# K3-F — panic SMP coordinato

Snapshot del progetto iniziale, 29–30 ottobre 2026. Le frasi sul boot BSP e
sugli AP non avviati descrivono quel checkpoint; lo stato e le prove
correnti sono in [k3i-runtime-validation.md](k3i-runtime-validation.md).

29 ottobre 2026. L'utente ha scelto il sistema completo, opzione B di
[k3f-design.md](k3f-design.md). Questo documento ne definisce il contratto
concreto. Aggiornamento 30 ottobre: scelta confermata per NMI diretti;
backend implementato e verificato sul BSP. Le [prove K3-F](k3f-validation.md)
distinguono questo risultato dal gate SMP ancora aperto.

## Obiettivo e riscontri

Un solo coordinatore del panic, record privati, arresto richiesto agli
altri core ammessi, attesa limitata e diagnostica d'emergenza. Nessun
ritorno al lavoro ordinario, rilascio di pagine o reset automatico.

Nel percorso ordinario `kernel_panic` usa ancora il logger e arresta soltanto il chiamante.
Il terminale Assembly delle eccezioni ha una guardia globale, ma non un
protocollo di arresto remoto. Il registro K3-C indica CPU abilitate, non
CPU OS operative. Il getter dei contesti resta BSP-only. Il mapping LAPIC
permanente esiste; i diagnostici isolati attivano il nuovo protocollo.
Gli accessi di configurazione temporanei usano invece
la finestra condivisa e non sono riutilizzabili nel panic.

La seriale ordinaria modifica `serial_ready` e puo essere usata da percorsi
normali: l'elezione del reporter non la rende automaticamente SMP-safe.
Il percorso d'emergenza deve evitare quei metadati e le code normali.

## Fonti primarie e profilo iniziale

[Intel SDM Vol.3A, 253668-078US](https://cdrdv2-public.intel.com/671190/253668-sdm-vol-3a.pdf):
§6.14.1/2/5, gate x64, frame e IST; §11.6.1, Figura 11-12, ICR xAPIC.
ICR low/high a offset 0x300/0x310, delivery mode bits 10:8, NMI=100b,
stato pending bit 12, destinazione fisica in high[31:24], shorthand 19:18.
Scrivere low avvia l'invio; pending azzerato non e un acknowledgement
del codice remoto. Questi offset sono relativi alla base LAPIC verificata,
non autorizzano a codificare un indirizzo fisico universale.

[Linux v6.12, arch/x86/kernel/smp.c](https://github.com/torvalds/linux/blob/v6.12/arch/x86/kernel/smp.c),
`native_stop_other_cpus`: elezione atomica, IPI ordinario seguito da NMI
ai core mancanti, attese distinte. La variante Linux che puo attendere
indefinitamente non soddisfa il nostro contratto. Riferimento
GPL-2.0-or-later; nessuna copia di codice o dei timeout.

[QEMU v11.1.0, hw/char/serial.c](https://github.com/qemu/qemu/blob/v11.1.0/hw/char/serial.c):
UART 16550A emulata, THRE=bit 5 di LSR, DLAB=bit 7 di LCR, accessi al
trasmettitore condizionati da DLAB. Fonte del dispositivo virtuale scelto,
non prova su UART fisiche. I/O base COM1 viene dal profilo gia inizializzato.

Prima versione: xAPIC gia validato, massimo 64 identita K3-C, root kernel
condivisa e mapping permanenti. x2APIC, hotplug, KPTI e nuovi backend UART
restano gate separati; nessuna istruzione o registro fuori dal profilo.

## Ownership, registrazione e atomiche

Modulo unico per-CPU/panic, con C nella cartella funzionale, Rust in
`rust/coordinator/` e Assembly in `assembly/entry/`. Rust gestisce elezione,
maschere e transizioni senza allocazioni; C possiede risorse e record;
Assembly cattura il contesto prima di entrare nei servizi del panic.

Maschere distinte: identita conosciute, core ammessi al protocollo e core
nel percorso terminale. PREPARED non entra nella maschera dei core ammessi.
Un'origine registrata con record privato può eleggere il reporter prima
dell'ammissione; questo non la rende un target remoto operativo.
L'ammissione richiede record stabile, TSS/IST e ingresso NMI pronti;
il futuro avvio AP deve rispettarla prima di dichiarare il core operativo.

Stato atomico one-shot per il coordinatore; nessun ticket lock o mutex.
Gli altri ingressi non sostituiscono il vincitore e non stampano. Un fault
del coordinatore durante il panic lascia stato incompleto e arresta quel
core senza ricorsione di logging o attesa del proprio lock.

Ammissione ed elezione devono avere un ordinamento comune verificabile.
Proposta: atomiche sequentially consistent per pubblicazione del bit
di ammissione, ricontrollo del coordinatore, elezione e snapshot dei target.
Un core ammesso dopo lo snapshot deve vedere il panic e arrestarsi senza
tornare al lavoro normale. Non basta una coppia di letture relaxed.
L'invariante deve essere provato con interleaving e stress host.

Per ogni CPU un record di 512 byte allineato a 64: negli AP, offset 256
della pagina metadati K3-D; per il BSP storage statico dedicato. Formato
versionato e assert C/Assembly per tutti gli offset. Contiene tipo evento,
identita, frame, GPR e registri di controllo disponibili. Per panic
volontario il contesto e quello dell'ingresso, non una ricostruzione
inventata di tutti i registri del chiamante prima della CALL.

EMPTY -> WRITING -> COMPLETE; pubblicazione release e lettura acquire.
Nessuna riscrittura dopo COMPLETE, nessuna lettura del payload come valido
prima di COMPLETE. La finestra metadati non cresce e non richiede lease
nuove; verifier di risorse e diagnostici dovranno riconoscerne il formato.

## Ingresso e percorso terminale

Nessuna dipendenza obbligatoria da GS nel primo ingresso: identita da
snapshot immutabile e backend verificato. Un core non identificabile non
puo indicizzare un array arbitrariamente. Prima che esistano identita,
mapping LAPIC e clock validati resta un terminale early locale esplicito;
non puo promettere un arresto remoto. Nessun AP viene ammesso in quella fase.

GPR/frame vengono catturati prima del C; nessuna funzione normale puo
ricostruirli dopo l'ingresso. Stack privati con guardie, IST separati per
NMI e double fault. SS nullo negli ingressi annidati va trattato secondo
il formato hardware. Nessun ritorno dal fault/NMI di arresto; non si
conserva uno stato SIMD per riprendere un'esecuzione che non riprendera.

I core secondari pubblicano il proprio record e confermano l'ingresso
nel percorso terminale, poi CLI/HLT senza IRET. La conferma non prova che
la CPU abbia gia eseguito l'istruzione HLT: e l'impegno irrevocabile a
non tornare al kernel normale. Le prove QEMU osserveranno anche lo stato CPU.
Un secondo NMI/fault non azzera il record o ripete gli acknowledgement.

## Invio e deadline — scelta specifica da discutere

A, scelta dall'utente e implementata: NMI direttamente ai soli target ammessi,
escluso il vincitore, con destinazioni fisiche esplicite. Nessun broadcast
a CPU ancora nel firmware o in attesa SIPI. Gli AP iniziali terranno IF=0:
un IPI ordinario potrebbe restare pendente fino al timeout.

B: prima IPI mascherabile, poi NMI ai mancanti, come il riferimento Linux.
Richiede un vettore di arresto e acknowledgement/EOI aggiuntivi; offre
una prima fase meno invasiva quando gli IRQ sono attivi, ma non sostituisce
gli NMI e aumenta il tempo massimo prima dell'escalation.

In entrambi i casi invio senza finestra temporanea o lock, sul mapping
LAPIC permanente predisposto prima del panic. ICR occupato viene atteso
entro il budget; nessuna scrittura cieca sul pending o su bit riservati.
Le scritture high/low sono gestite dallo stesso core; gli ingressi ricorsivi
non competono sul trasmettitore ICR.

Proposta iniziale misurabile: budget globale di arresto 100 ms sul PM
timer esistente, piu limite globale di un milione di controlli. Non sono
100 ms per ciascun core. Clock fermo o lettura fallita terminano l'attesa.
Prima del clock validato solo limite di operazioni, senza dichiararlo
equivalente a una durata. Soglie da verificare sul profilo, non universali.

Alla scadenza conservare maschera dei mancanti e causa: non dichiarare
tutti i core arrestati. NMI bloccato, SMM, APIC guasto o core non responsivo
possono impedire la cooperazione. Nessun reset automatico per nasconderlo.
ACK tardivi possono aggiornare i record, senza riscrivere lo snapshot
dei mancanti usato dal rapporto finale.

## Diagnostica d'emergenza e guasti ulteriori

Solo il vincitore puo tentare seriale polled, senza logger/queue/mutex,
allocazioni o rendering video. Etichette fisse e dati numerici dai record;
puntatori a messaggi arbitrari non vengono dereferenziati nel panic.
Registrare eventualmente il puntatore come dato forense, non come stringa.

Output massimo 2048 byte, budget temporale separato proposto 250 ms e
limite globale di operazioni. UART assente/bloccata o DLAB inatteso:
conservare il rapporto in RAM e arrestarsi senza riprogrammare alla cieca
il dispositivo. Troncamento e fallimento seriale sono stati del rapporto.
Se mancano conferme remote, non garantire output privo di interferenze da
un core non fermato. La RAM resta la fonte primaria della diagnostica.

Nessun rilascio di memoria, pulizia di code device o arresto DMA implicito.
Le risorse rimangono possedute e pinned. Il panic CPU non e un protocollo
di shutdown completo dei dispositivi. Corruzione delle pagine del panic,
triple fault o fault ricorsivo possono impedire un record completo: mai PASS.

## Verifica, dipendenze e avanzamento

1. Protocollo e formati: Rust atomico/no_std, ownership C e test host di
   elezione simultanea, ammissione concorrente, duplicati, ricorsione,
   timeout, ACK tardivi, 1/2/4/64 identita. Nessun reset operativo del panic;
   i test usano istanze nuove isolate. ASan sul C; stress host non e SMP guest.
2. Ingressi/trasporto: NMI, IDT/TSS/IST, LAPIC, clock e seriale d'emergenza;
   prove BSP reali, mock negativi per guasti non riproducibili sul device,
   mapping/lease mantenuti. Il terminale BSP e kernel_panic entrano nel
   medesimo protocollo quando pronto; fallback early distinto.
3. Integrazione con l'avvio AP: almeno dieci prove finali con quattro core
   realmente avviati, fault sul BSP e AP, doppio panic, IF=0, lock detenuto,
   core senza ACK, seriale bloccata e verifica dei record/stati CPU.
   Dieci boot ordinari finali di regressione e casi BSP/assenza AP separati.

Oggi gli AP non vengono avviati: il punto 3 dipende dal trampolino e
dall'ingresso ancora da costruire. Preparare o compilare il sistema non
ne chiude il gate SMP. Le prove di avvio isolate appartengono all'integrazione
concordata; qualsiasi estensione produttiva dell'avvio va progettata prima.

Perimetro: panic per-CPU e soli adattatori necessari a eccezioni/NMI,
descrittori, ammissione CPU, accesso LAPIC permanente, clock PM, seriale,
bootstrap, build e test. Nessuna riscrittura generale di IRQ, scheduler,
allocator o driver. Riutilizzo di `reuse/` escluso. Fonti e diff per questa
stesura iniziale sono storici; build e prove finali del backend BSP sono
registrate nel [rapporto di validazione](k3f-validation.md). Il punto 3
resta da eseguire dopo l'avvio effettivo degli AP.
