# Stato corrente — 2 novembre 2026

Questo è il riferimento breve. I checkpoint storici conservano le prove
relative alle proprie immagini; non certificano automaticamente la working tree.
Lo [snapshot del 22 ottobre](review/status-2026-10-22.md) è conservato separatamente.

## Funzionalità presenti

Boot UEFI x86-64, ELF e contratto v5, paging NX/WP, allocatore fisico,
finestre/arena, heap slab e page-run, GDT/TSS/IST, eccezioni e IRQ.
Scheduler cooperativo BSP con stack reali; tastiera e mouse PS/2, shell,
console GOP e console VirtIO con dominio Intel VT-d dedicato alla GPU.
Non ci sono ancora SMP operativo, processi utente, syscall, VFS o driver disco.
La lettura dei file di boot usa UEFI nel loader, non un filesystem del kernel.

## K3: base per-CPU chiusa sul profilo QEMU

K3 completata per il profilo xAPIC QEMU/OVMF: BSP e tre AP inizializzati,
AP parcheggiati, contesti privati e panic coordinato verificati. Campagna
finale 80/80: 30 boot ordinari/console e 50 panic intenzionali distinti.
Nel caso conteso tutti e quattro i core hanno vinto almeno una volta.
Passata anche una sessione continua di 300,953 secondi con 808 pacchetti
mouse confermati, pixel, shell, cleanup e accounting. Sette suite host
pertinenti passate; corretto un difetto di polling nel solo collaudo.
Scheduler ancora BSP: task AP, hotplug e prove su hardware fisico restano
fuori da questa chiusura. [Prove e limiti](../arch/x86/kernel/cpu/percpu/documentation/k3i-runtime-validation.md#chiusura-k3).

K3-I/boot ordinario: il BSP ora avvia e parcheggia i tre AP e prosegue
fino alla console. Passati 10/10 avvii a quattro CPU, 10/10 della stessa
immagine a una CPU, 10/10 panic dopo timer/scheduler, 10/10 sessioni console
GOP con input/pixel/cleanup e controllo finale degli AP, più 10/10 del
diagnostico sequenziale precedente. Il checkpoint precede la chiusura
descritta sopra. [Prove e limiti](../arch/x86/kernel/cpu/percpu/documentation/k3i-runtime-validation.md).

I paragrafi seguenti descrivono i checkpoint precedenti; le indicazioni
«boot ordinario BSP» sono superate dal risultato sopra.

K3-I/panic sul gruppo: il diagnostico ora verifica anche l'arresto di tutti
e quattro i core, con origine volontaria BSP oppure eccezione NMI su AP3.
Passati 10/10 boot per origine: un reporter, tre ACK, nessun core mancante,
quattro record completi e stack/IST/root reali verificati. Suite host C/ASan
e Rust passata; nessuna nuova modifica al backend panic di produzione.
L'integrazione ordinaria è ora provata sopra; resta lo stress finale di K3.
Le task AP non sono implementate. [Prove e limiti](../arch/x86/kernel/cpu/percpu/documentation/k3i-group-panic-validation.md).

K3-I/sequenziale: riarmo della sola pagina dati dopo PARKED implementato.
Il diagnostico inizializza e parcheggia BSP+tre AP reali: 10/10 boot del
gruppo, negativo dopo scrittura 1/1, 31 scenari host C/Rust con ASan e due
test del monitor passati. Regressioni primo AP e ordinario 10/10 ciascuna.
Codice, tabelle e risorse restano trattenuti. Boot ordinario e scheduler
ancora BSP; panic da AP/gruppo ora provato sopra, integrazione ordinaria
e stress ancora da completare.
[Prove e limiti](../arch/x86/kernel/cpu/percpu/documentation/k3i-sequential-validation.md).

K3-I/panic remoto: dopo il primo AP, il BSP ha provocato il panic reale
e ricevuto l'ACK dell'AP tramite NMI/IDT/IST2 definitivi. Passati 10/10
panic remoti e 10/10 avvii senza panic; suite host C/ASan e Rust passata,
audit degli oggetti d'emergenza senza dipendenze FP/SIMD rilevate.
In quel checkpoint: online BSP+un AP, targets=ACK=2, nessun core mancante.
L'avvio degli altri AP e l'origine panic da AP sono ora provati sopra;
resta integrazione ordinaria, con scheduler sempre BSP.
[Prove e limiti](../arch/x86/kernel/cpu/percpu/documentation/k3i-remote-panic-validation.md).

K3-I/primo AP: il diagnostico INIT/SIPI/NMI ha ora eseguito davvero
l'ammissione C e il ritorno sul core 1: stack/GS/GDT/TSS privati e IDT finale condivisa,
provider accettato e PARKED verificati nei registri reali. Passati 10/10
boot dedicati, negativo senza NMI 1/1, 50 casi host e regressioni 10/10
joint-panic e 10/10 ordinarie. Corrette una race del supervisore e la
verifica dei bit A/D del test. È BSP+un AP parcheggiato, non quattro CPU
operative; il boot ordinario resta BSP. Nel checkpoint restavano altri AP,
panic remoto (ora provato sopra) e integrazione ordinaria.
[Prove e limiti](../arch/x86/kernel/cpu/percpu/documentation/k3i-first-ap-validation.md).

I paragrafi seguenti sono checkpoint precedenti: le frasi «nessun AP
avviato» descrivono le rispettive campagne, superate dalla prova sopra.

K3-I/contesti: provider AP con binding ID/GS e stato atomico, getter PREPARED
e scansioni dell'arena chiusi prima dell'ingresso. Catena alta Assembly/C
compilata con piano/IDT/panic e PRIVATE/PARKED; non ancora selezionata dal
record installato o avviata tramite INIT/SIPI. Passati 120 casi host clock/
runtime, 50 BSP e 20 identità, 17 prove CPU del ponte con callback modellati,
10/10 UEFI dedicati e 10/10 ordinari. Nessun AP avviato; ammissione C positiva
su AP ancora da provare. [Prove e limiti](../arch/x86/kernel/cpu/percpu/documentation/k3i-context-validation.md).

K3-I/gruppo CPU: scelta acquisita e descrittore unico implementato nel
diagnostico joint-panic, con pubblicazione release/acquire dopo sigillo e
preparazione panic. Passati 5 casi Rust debug/release, 20 processi host con
4 lettori concorrenti ciascuno, 10/10 UEFI dedicati e 10/10 ordinari.
Monitor verifica snapshot e binding reali; nessun AP avviato, nessuna
promozione ACTIVE o abilitazione dei provider AP in quel checkpoint.
Il blocco contesti sopra aggiorna il codice, senza ancora provarlo su AP.
[Prove e limiti](../arch/x86/kernel/cpu/percpu/documentation/k3i-group-validation.md).

K3-I/prerequisiti ammissione: corretto il controllo degli alias degli stack
di emergenza nei validator ingresso/ritorno, incluso overflow del top IST.
Host C/Rust/ASan e regressioni joint passati; 17/17 esperimenti CPU del
ritorno, 10/10 UEFI joint-panic e 10/10 ordinari aggiornati. Nessun AP
avviato. Progetto del descrittore d'ammissione preparato; scelta sulla
pubblicazione allora pendente, ora acquisita nel blocco gruppo CPU sopra;
nessuna promozione runtime implementata.
[Prove e limiti](../arch/x86/kernel/cpu/percpu/documentation/k3i-admission-validation.md).

K3-I/ritorno: piano C/Rust e helper IRETQ con controlli GS ACTIVE, GDT/TSS
e gate/IST. 17/17 esperimenti CPU isolati, inclusi NMI pendente e IRET
faulting. Collegata anche la preparazione panic dopo il sigillo, senza
secondo commit: 10/10 UEFI nuovi, 10/10 panic precedente e 10/10 ordinari.
Restano ammissione C degli AP e collegamento del ritorno al runtime; nessun
AP avviato. [Prove e limiti](../arch/x86/kernel/cpu/percpu/documentation/k3i-return-validation.md).

K3-I/commit congiunto: barriera irreversibile del gruppo, sigillo di blob e
tabelle, arena e contesti; errori successivi portano ad arresto diagnostico
con risorse trattenute. Verifica finale: 53 scenari host, 10/10 boot UEFI del
sigillo, due arresti intenzionali e 10/10 ordinari. SEALED non autorizza
ancora INIT/SIPI: nessun AP avviato e ingresso runtime ancora da completare.
[Prove e limiti](../arch/x86/kernel/cpu/percpu/documentation/k3i-joint-validation.md).

K3-I/installazione inattiva: blob e record scritti nelle lease fisiche vere
e riletti integralmente. Prestito esclusivo impedisce cleanup/ricostruzione
finché il contenuto non è cancellato e verificato. Revisione v3: 10/10 boot
UEFI installazione, 10/10 cancellazione, 10/10 ordinari; host con 26 failure
point d'installazione, 22 di cancellazione e 8192 corruzioni byte. Nessun
AP avviato o collegamento del prefisso alto. Il commit del gruppo è ora
verificato nel solo diagnostico descritto sopra.
[Prove e limiti](../arch/x86/kernel/cpu/percpu/documentation/k3i-installation.md).

K3-I/ingresso alto: prefisso Assembly con copia sullo stack privato, IDT/GDT
intermedie alte, passaggio al CR3 operativo, TSS e GS, inizializzazione
x87/SSE. Dieci prove consecutive e tre negativi passati sul BSP SeaBIOS,
con stato SIMD inizialmente alterato; non ancora collegato al
kernel. Restano ammissione runtime e collegamento al trasporto di avvio;
IRETQ e preparazione panic/IST hanno ora le prove distinte descritte sopra.
Installazione e sigillo sono verificati nei diagnostici
descritti sopra. Nessun AP avviato.
[Contratto e limiti](../arch/x86/kernel/cpu/percpu/documentation/k3i-private-entry.md).

K3-I/blob: template rilocabile 16/32/64 bit e packer C/Rust verificati su
host e in 14 esperimenti CPU (dieci avvii consecutivi, due posizioni e
quattro negativi, inclusi NX/WP). Sono prove isolate SeaBIOS sul BSP,
non AP del kernel. Il blob viene ora installato soltanto nel diagnostico
inattivo descritto sopra; restano ingresso alto definitivo e collegamento
del sigillo al protocollo di avvio.
[Prove e limiti del blob](../arch/x86/kernel/cpu/percpu/documentation/k3i-protocol.md).

K3-I/durata: congelamento irreversibile delle risorse inattive collegato
al kernel e verificato nella revisione v2 con 10/10 boot UEFI dedicati e
10/10 ordinari. Il registro del prestito impedisce rilascio e ricostruzione
anche con State alterato; ASan host e cppcheck passati. Non è ancora il
sigillo del blob o il commit congiunto AP se considerato da solo; queste
garanzie sono ora verificate dal diagnostico congiunto. Nessun AP avviato.
[Prove finali e limiti](../arch/x86/kernel/cpu/percpu/documentation/k3i-protocol.md).

K3-I/trasporto: sequenza INIT/SIPI collegata al coordinatore Rust e verificata
in 41/41 scenari host con ASan sul C e cppcheck. Deadline, errori APIC e
guasti dopo invio parziale portano a quarantena. I/O ancora modellato:
nessun backend MMIO nativo o avvio AP del kernel. [Dettagli e limiti](../arch/x86/kernel/cpu/percpu/documentation/k3i-protocol.md).

K3-I/NMI: approvato il passaggio dentro un vero handler NMI. Esperimento
CPU isolato SeaBIOS/TCG passato: dieci avvii consecutivi più tre controlli,
secondo NMI trattenuto attraverso il cambio di modalità e consegnato dopo
IRETQ. Non è il kernel UEFI e non avvia AP. [Report e limiti](../arch/x86/kernel/cpu/percpu/documentation/k3i-nmi-validation.md).

K3-I: scelto il diagnostico AP dedicato prima del boot ordinario.
Implementato il coordinatore Rust di avvio sequenziale, generazioni,
rollback pre-commit e quarantena terminale. Record d'ingresso C/Rust e
conferma legata a identità/token verificati: 16/16 casi Rust in debug e
release, due eseguibili C/Rust con ASan sul C e cppcheck passati. Non ancora collegato al
kernel: nessun AP avviato o gate SMP chiuso. [Prove e lavoro restante](../arch/x86/kernel/cpu/percpu/documentation/k3i-protocol.md).

K3-H: root AP condivisa preparata e ancora inattiva. Dati bassi separati
dal codice, quattro frame sotto 4 GiB, riferimenti kernel alti e nessun
ricorsivo nella nuova root; CR3 del BSP invariato. Revisione finale v5:
10/10 preparazioni, rollback 1/1, 10/10 ordinari e 10/10 sessioni console
VirtIO/VT-d passati, insieme a host C/Rust, ASan, differenziali e stress
allocatore. Risolta la scadenza console senza cambiare i 600 s o ridurre
ownership/bounds. Lo startup resta lento: osservazioni host circa 201–511 s,
non un benchmark controllato. Nessun AP avviato o nuovo CR3 attivato.
[Prove K3-H e limiti](../arch/x86/kernel/cpu/percpu/documentation/k3h-validation.md).

Aggiornamento K3-G: prenotata nel kernel, prima delle allocazioni generiche,
una pagina sotto 1 MiB per il futuro ingresso AP. Prima lease pinned,
selezionata dalla RAM ammessa; assenza gestita come indisponibilità SMP
senza fermare il BSP. Nessun mapping basso aggiunto al CR3 operativo del BSP; il codice di avvio manca ancora.
Dieci boot dedicati, dieci ordinari, negativo iniettato e console VirtIO/VT-d
passati; suite host e stress allocatore isolato passati.
[Evidenze K3-G e limite dello stress parallelo](../arch/x86/kernel/cpu/percpu/documentation/k3g-validation.md).

Implementato il protocollo scelto con NMI diretti: record privati,
coordinatore atomico Rust, cattura Assembly, trasporto LAPIC e seriale
d'emergenza con attese limitate. Attivazione per ora nei diagnostici;
il boot ordinario mantiene il precedente panic locale.

Immagini finali: 21/21 scenari diagnostici (dieci preparazioni consecutive
incluse), 10/10 boot ordinari e 1/1 console VirtIO/VT-d con input, pixel e
cleanup. Suite host panic e regressioni kernel/bootstrap/risorse passate;
verificato anche il percorso senza FP/SIMD. Un solo core OS operativo.
[Report K3-F e limiti delle prove](../arch/x86/kernel/cpu/percpu/documentation/k3f-validation.md).

Riserva bassa e tabelle inattive sono predisposte; il gate di integrazione
video K3-H è passato sulla revisione v5. Mancano trampolino e ingresso AP, poi prova coordinata con
quattro core realmente avviati. NMI remoto e ramo AP di ammissione non sono
ancora verificati in guest; quattro vCPU configurate non chiudono K3-F.

Il vecchio stato «DMA inattivo» riguarda i checkpoint precedenti. La
console VirtIO/VT-d persistente è stata verificata nella campagna V2c
`qemu-run-0e1e80c861594423ab6e9e7e662e7e17` (10/10) sulla tappa 5b.
Non dimostra compatibilità con hardware fisico.

## K1: metà alta e revoca del loader

Le tappe 1–5 del layout sono già nel repository (tappa 5: `b3eb7e2`).
La tappa 5b rimuove PML4[0] soltanto dopo avere verificato stack e
GDT/TSS/IDT del kernel. Le pagine del loader rimangono riservate.

Il nuovo test dedicato ha verificato dieci boot consecutivi con lettura
fisica indipendente di tutte le voci PML4 basse. Il negativo scalda la
traduzione della capsula prima della revoca e verifica #PF dopo MOV CR3.
K1 è chiuso nel profilo QEMU: 23/23 scenari kernel, 12/12 campagne brevi
e 10/10 console VirtIO con VT-d. Tutte le 37 suite host sono coperte e
passate tramite run generale e riesecuzione mirata del controllo di formato.
La formattazione finale non cambia codice/layout: il confronto degli ELF
bootstrap e VirtIO rileva soltanto il timestamp della directory debug PE.

[Contratto 5b](../arch/x86/mm/paging/retirement/documentation/design.md) e
[prove, immagini e limiti](../arch/x86/mm/paging/retirement/documentation/validation.md).
[Roadmap K1–K6](design/k1-higher-half.md).

## Verifiche e limiti

Test in `test/`, script in `scripts/`, artefatti in `build/`.
`./scripts/test/run.ps1` esegue le suite host; `./scripts/test/qemu.ps1`
esegue le campagne registrate, incluse `paging-retirement` e
`paging-retirement-fault`. Coverage di linee/branch non misurata.

L'errore di formato preesistente in `configuration/read.c` è risolto:
sono stati tolti solo i due spazi finali autorizzati dall'utente. Il
report iniziale fallito resta conservato; il riepilogo cita il retry.
ASan riguarda i test host; non certifica assenza di leak dell'intero kernel.
Le pagine del loader non vengono ancora recuperate. Nessuna prova fisica.

## K2: sincronizzazione

K2-A è implementato e verificato: ticket lock C, atomiche Rust e backend
IF/PAUSE x86, con attesa sorvegliata, guard, ranghi e release LIFO.
Test: 3 Rust in debug/release, C ASan, 140.000 acquisizioni FIFO su 2/4/8
thread host, sette fail-stop attesi; dieci boot diagnostici positivi,
dieci ordinari e due negativi QEMU. I core AP non sono avviati.
[Prove e limiti](../kernel/sync/documentation/validation.md).

Il runtime sync è collegato al kernel ordinario dal 28 ottobre, con
contesto BSP inizializzato prima della memoria. K2-B adotta ora lock per
owner nell’allocatore fisico, query coerenti piccole per budget/range/lease
e rilascio dopo unmapping con pin conservato se manca capacità. I lettori
dei metadati nei chiamanti sono migrati alle query; nessuna operazione
hardware o callback sotto il lock. Anche il core heap adotta ora lock e
prenotazioni durante il fill; holdings e lifecycle dei mapper restano BSP. **K2 è verificato nel profilo concordato
dal 29 ottobre:** smoke completo 17/17 e dieci boot aggiuntivi dell’ELF heap
ricompilato. [Chiusura e limiti K2](../kernel/sync/documentation/k2-integration.md).
Gli AP non predisposti sono rifiutati;
GS/per-CPU sul BSP è ora verificato in K3-A. Contesa verificata con thread host, non SMP guest.
[Contratto approvato](../kernel/mm/physical/documentation/k2-design.md).
[Prove del pool e limiti](../kernel/mm/physical/documentation/k2-pool-validation.md).
La [preparazione precedente](../kernel/mm/physical/documentation/k2-core-validation.md)
resta uno snapshot storico. Nel blocco pool le 39 suite host sono verificate
in aggregato, incluse le ripetizioni dei tre adattatori mock corretti; sulle
immagini di quel blocco passano dieci sessioni GOP e una VirtIO/IOMMU, con
input, pixel e cleanup verificati. Il blocco heap successivo passa ASan, 200.000
operazioni casuali, 14.000 cicli concorrenti host, 40 trasferimenti sospesi
e dieci boot diagnostici; [prove heap](../kernel/mm/heap/documentation/k2-validation.md).
Prossimo blocco: completamento K3 per AP, dati e clock di contesa per CPU.
Nessun AP è ancora avviato.

Riscontro storico dell’integrazione K2-A: un boot terminava in LAPIC_TIMER_INVALID dopo
il probe lock riuscito. Aggiunta diagnostica timer con autorizzazione;
i successivi 30 boot passarono senza isolare la causa. Questi retry
non erano una correzione; il fix successivo e il limite dell’attribuzione
sono distinti sotto. Dettagli nel report di sincronizzazione.

Su scelta dell'utente è corretto un errore concreto della misura:
arm→IRQ e ritorno del watchdog sono separati, con soglie invariate.
Passano dieci boot K2, dieci ordinari, ciclo continuo, negativo IRQ perso,
regressioni C/Rust e ASan. La causa esatta del vecchio panic resta non
attribuita: questo fix non è una prova retroattiva.
[Implementazione e risultati](../arch/x86/kernel/timer/lapic/documentation/wait/k2-failure.md).

Restano K3 AP/dati per CPU, K4 shootdown/preemption, K5
syscall/user copy e K6 ELF utente. Nessuna di queste funzionalità viene
indicata come già implementata.

## K3-A: contesto BSP tramite GS

Passaggio early → runtime implementato dopo GDT e ritiro dello stack iniziale,
con lock rilasciati e IF preservato. Il vecchio contesto viene azzerato;
contatore dei token e binding del clock sono conservati. Il reader valida
la base GS prima di dereferenziarla, senza fallback allo stato ritirato.
Passano 50 esecuzioni host con ASan, dieci boot diagnostici, dieci ordinari
e due negativi; passa anche una regressione interattiva VirtIO/VT-d.
Il monitor verifica GSBASE e memoria dei due contesti.
Quattro vCPU configurate non significano quattro core avviati: solo BSP.
K3 resta aperto per completamento delle risorse per CPU, ingresso e avvio AP.
[Report K3-A](../arch/x86/kernel/cpu/percpu/documentation/k3-validation.md).

## K3-B: clock sorvegliato per i lock BSP

Collegata la sorgente ACPI PM esistente dopo la discovery, con descrittore
e dati copiati in memoria persistente del modulo per-CPU. Verifica del
progresso con IF=0; bind unico e quiescente, rollback prima della pubblicazione.
Nessun lock o allocazione nel callback. Policy di bring-up: 100 ms oppure
un milione di controlli, al primo limite; non un benchmark dei driver.

Passano 50 esecuzioni della nuova suite host e le regressioni percpu, sync,
clock e bootstrap; C ASan e analisi statica pertinenti. Sul kernel finale:
dieci boot diagnostici, dieci ordinari, due negativi e una sessione completa
console VirtIO/VT-d con pixel, input e cleanup. Coverage N/D, nessuna prova
fisica. [Report K3-B](../arch/x86/kernel/cpu/percpu/documentation/k3b-validation.md).

Solo BSP collegato. Prima degli AP restano identità, risorse/contesti
completi, protocollo di ingresso e avvio, più le prove SMP reali della roadmap.

## K3-C: registro delle identità CPU

Implementato lo snapshot di boot posseduto: indice logico distinto da ID
APIC e UID ACPI, BSP verificato all’indice zero, capacità 64, CPU offline
contate separatamente. Rust valida i campi e le identità; C pubblica solo
il piano completo e rifiuta doppia inizializzazione e alias.

Passano due test Rust in debug/release, 20 esecuzioni C ASan con tutti i
64 posizionamenti del BSP e tre suite host di regressione. Sul kernel
finale: dieci boot diagnostici, dieci ordinari, due negativi e una console
VirtIO/VT-d completa. Il monitor confronta lo snapshot con i record MADT
e il contesto GS reali. [Report K3-C](../arch/x86/kernel/cpu/percpu/documentation/k3c-validation.md).

Gli AP non sono avviati. Hotplug e backend x2APIC operativo restano fuori
perimetro. Il pianificatore sa ignorare Flags=0, ma il parser MADT esistente
li rifiuta prima della cattura: questa compatibilità nativa resta da
rivedere. Prossimo blocco K3: risorse private e ingresso AP, non ancora SMP.

## K3-D: risorse private AP predisposte

Arena dedicata con una lease dati e una tabelle, pinned; per AP stack
64 KiB, tre IST da 16 KiB, guardie e pagina GDT/TSS privata. RW/NX,
supervisor, con validazione Rust della geometria e riconoscimento esplicito
nell’inventario paging, video e GPU. Con tre AP: 348 KiB dati + 12 KiB tabelle.
Il solo BSP non prenota risorse AP. Nessun descrittore AP viene caricato.

Passano la nuova suite C ASan/Rust (101 failure point, 64 geometrie,
63 sweep dei descrittori, cleanup e pin conservati su rifiuto) e dieci
suite host di regressione pertinenti. Immagini finali: dieci boot
diagnostici, dieci ordinari, due fault guard/NX attesi e una sessione
VirtIO/VT-d con quattro vCPU configurate, pixel/input/cleanup verificati.
Le prime regressioni video/GPU sono state corrette e restano documentate;
i risultati finali non riusano le loro immagini. [Report K3-D](../arch/x86/kernel/cpu/percpu/documentation/k3d-validation.md).

Al termine di K3-D restavano commit, contesti/clock/GS degli AP e percorso
di ingresso. Le risorse PREPARED non dimostrano SMP.

## K3-E: contesti AP e risorse non rilasciabili dopo commit

Contesti lock privati e attese PM predisposti per gli AP registrati, senza
copiare token o frame del BSP. Il boot ordinario conserva il rollback.
La nuova API di commit verifica i contesti e blocca rilascio delle lease,
azzeramento dei contesti e delle attese prima del futuro tentativo di avvio.
Nessuna nuova allocazione heap, istruzione privilegiata o attivazione AP.

Passano sei suite host pertinenti: clock (70 esecuzioni), risorse C/Rust,
provider GS, identita, kernel e bootstrap. Dieci boot finali diagnostici
con quattro vCPU e dieci ordinari alternando una/quattro vCPU; monitor
dei contesti e delle lease. Passa anche una sessione console VirtIO/VT-d
con quattro vCPU, pixel/movimento/cleanup verificati.
[Report K3-E](../arch/x86/kernel/cpu/percpu/documentation/k3e-validation.md).

Resta un solo core OS attivo. Mancano compatibilita fault/NMI per CPU,
riserva bassa prima delle allocazioni generiche, trampolino/CR3 di
transizione, ingresso e INIT/SIPI, poi prove di piu core realmente pronti.
