# K3-I — contesto AP e collegamento alto, 1 novembre 2026

Implementata la continuazione del [gruppo immutabile](k3i-group-validation.md).
Il provider dei contesti supporta ora gli AP attraverso un gate atomico;
la catena alta Assembly/C è compilata e collegata internamente nel solo
diagnostico. Nessun AP del kernel viene ancora avviato: il record installato
dal diagnostico resta diretto al suo stub, non al nuovo prefisso operativo.
K3-I non è dichiarato pronto e non si passa a K4.

## Ownership e sincronizzazione

Il BSP chiude irreversibilmente i getter PREPARED dopo lo snapshot.
Verifica prima i contesti sigillati, copia gli ID attesi dallo snapshot e
pubblica count con release. Gli AP acquisiscono count prima di usare la
tabella ID. Nessun AP può promuovere il contesto prima della chiusura.

Per ciascun core una parola atomica separata espone 0 iniziale, 1 ingresso
riservato, 2 provider accettato, 3 errore terminale. Soltanto la CPU con
l'identità hardware corrispondente può modificarla, con IF=0. Lo stato 1
non autorizza letture remote dei campi ordinari. Il campo State nel layout
GS resta un dato locale; non viene usato come mailbox fra CPU.

Il provider confronta ID hardware e area GS conosciuta prima di leggere
Self o i metadati del contesto. Rifiuta un GS che identifica il contesto di
un altro core. L'AP non legge la vecchia fase non atomica del bootstrap BSP.
Wait e sorgente PM restano quelli predisposti; x86_percpu_wait_current può
selezionare il proprio slot AP solo dopo l'accettazione. Gli errori chiudono
il provider e non consentono ripristino PREPARED o rilascio delle risorse.

Corretto anche il preflight delle risorse: dopo la chiusura non esegue più
scansioni dell'arena privata, che un AP potrebbe modificare. I controlli
iniziali zero/ownership restano prima della chiusura. Il diagnostico inserisce
un callback sentinella e richiede che committed_valid, joint_ready e preflight
non lo invochino dopo la chiusura. Questo corregge un percorso di lettura
che sarebbe stato scorretto con AP attivi; non attesta una race già riprodotta.

## Catena nativa predisposta

Il ponte verifica RSP/frame e allineamento prima di chiamare C, riserva
32 byte di shadow space e conserva il frame in R15. La continuazione C:

1. Acquisisce il gruppo, identifica la CPU e verifica root e indirizzo
   atteso del frame prima di dereferenziarlo; controlla lo stack corrente.
2. Valida record/token con Rust e li riconcilia con identità, geometria,
   root/PAT e area locale posseduti. Verifica profilo APIC, ambiente TSS,
   IDT finale e binding del wait; prova avanzamento PM con limite finito.
3. Prepara il piano di ritorno e reclama ARMED -> ENTERED tramite Rust.
   Riserva il contesto locale, carica l'IDT finale e richiede l'ammissione
   al panic, che ricontrolla GDT/TR/TSS/IST e IDTR effettivo.
4. Accetta il provider solo dopo questi controlli, verifica contesto/clock
   correnti e pubblica PRIVATE. Il ponte passa all'helper IRETQ esistente.
5. Dopo IRETQ la seconda continuazione verifica il contesto e pubblica
   PARKED prima del terminale HLT. Nessun accesso alla memoria bassa rimane
   necessario. Non è un ingresso nello scheduler SMP.

I codici nonzero sono terminali: 1/2 ingresso o frame/root non ammessi,
3 record/stack, 4 ambiente/clock/IDT, 5 piano, 6 claim, 7 contesto locale,
8 panic/provider, 9 conferma PRIVATE; 10–13 riguardano il parcheggio.
Prima di conoscere il frame il codice non lo scrive. Dopo la verifica,
errore e fase locale precedono la pubblicazione release dello stato 3;
un futuro lettore può acquisire questo stato per leggere la diagnostica.
La quarantena Rust viene richiesta solo per un token già reclamato o
riconciliato nel percorso di parcheggio. Nessuna liberazione post-barriera.

La funzione di ammissione richiede l'ingresso nel protocollo NMI bloccato
già approvato; non deduce tale stato dal solo IF=0. Il successo delle API
di basso livello del contesto non è, da solo, prova di ammissione hardware.

## Prove finali, con confini distinti

- Host contesti/clock: kernel/build/k3i-context-host-v5.log, 120 processi:
  50 percorsi PM precedenti, 20 arresti intenzionali catturati dal modello,
  50 casi runtime. Coperti 4/64 contesti, ID sparsi 0/7/42/254, ingresso
  duplicato, GS errato/di altro core, binding alterato, provider chiuso
  prima di accept, errore terminale e lock reali con IF ripristinato.
- Regressioni BSP e identità: kernel/build/k3i-context-bsp-host-v5.log
  (50 processi) e k3i-context-identity-host-v5.log (20). C compilato /W4 /WX,
  ASan sul C; registri, identità CPU e I/O PM sono modellati.
- Ponte CPU finale: kernel/build/ap-return-cpu-f95373a1ce274d59bf816c7faa8d922d/,
  17/17: dieci consecutivi e sette controlli/negativi, compreso IRET faulting.
  Include prefisso, resume e ponte Assembly del prodotto. I callback C sono
  sostituiti da stub Assembly che controllano ABI e scrivibilità dello shadow
  space, alterando i registri volatili; il ritorno passa a quello reale del
  ponte attraverso un thunk diagnostico. Non esegue la nuova ammissione C,
  il provider reale o un AP: è una CPU BSP SeaBIOS/TCG con GS predisposto.
- UEFI contesti: kernel/build/ap-joint-firmware-2e35e759615742c68c3538cbbe098945/,
  10/10 con kernel/build/k3i-context-joint-v5/kernel.elf. Verificati snapshot,
  getter chiusi, scansioni bloccate, rifiuto delle entrate AP sul BSP e
  maschera panic ancora pari al solo BSP. Il percorso C nativo positivo
  non è eseguito da questa campagna.
- Ordinario: kernel/build/percpu-firmware-7dd4c453872246dc988d30c207d88e5f/,
  10/10 con kernel/build/k3i-context-ordinary-v5/kernel.elf.
- Cppcheck finale: kernel/build/k3i-context-static-v5/, nessuna diagnostica
  bloccante; informazioni sugli include conservate. Audit oggetti senza
  helper di allocazione o __chkstk. Non è una misura completa dello stack.

Manifest: kernel/build/k3i-context-manifest.json. Source audit:
kernel/build/k3i-context-source-audit.json. Prove su versioni precedenti
conservate separatamente, non sommate ai dieci boot di ciascun profilo finale.
Nessuna nuova allocazione dinamica: nuovi metadati statici e frame già
previsto. ASan non strumenta Rust/Assembly o RAM guest; Valgrind, TSan,
coverage e hardware fisico N/D. I thread/CPU modellati non dimostrano SMP.

I wrapper rimasti aperti dopo i marker finali sono stati chiusi dopo la
verifica di log, ELF e risultati. Il loro exit code d'interruzione non è
contato come PASS dei test; i runner CPU e i due runner UEFI finali hanno
terminato con exit code 0. Le invocazioni host sono controllate dallo script
prima del proprio marker finale. Audit sorgenti: massimo 245 righe nei
file interessati, cartelle funzionali entro cinque file diretti.

## Fonti e gate ancora aperti

[Microsoft x64 ABI](https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention?view=msvc-170):
Calling convention defaults e Alignment, RCX/RDX, R15 nonvolatile,
shadow space e allineamento. [Intel SDM 253668-078US](https://cdrdv2-public.intel.com/671190/253668-sdm-vol-3a.pdf)
§§6.7.1 e 6.14.1/3–5 per NMI, gate, IST e IRET; RFLAGS IF9, DF10,
IOPL12–13, NT14. CPUID, MSR, lettura GDT/TR e caricamento IDT riusano i
backend esistenti; nessun nuovo offset MMIO. Le sorgenti ABI sono state
ricontrollate prima del nuovo ponte. Release/acquire riusa le primitive Rust.

Restano selezione del prefisso nel vero record di avvio, collegamento
INIT/SIPI/NMI nativo, primo AP reale, riuso controllato dei dati transitori
solo dopo PARKED, avvio sequenziale completo e panic remoto. L'assenza
di una prova positiva dell'ammissione C su AP impedisce di dichiararla pronta.
