# Scheduler: collegamento IRQ LAPIC → eventi → task

Snapshot storico del blocco IRQ/task. La successiva integrazione e le prove
sui sorgenti aggiornati sono nel [report idle supervisionato](../../idle/documentation/validation.md).

8 ottobre 2026. Profilo v0 scelto dall'utente: IRQ durante l'esecuzione delle
task. [Contratto e fonti](design.md). Questo report riguarda il solo modulo
funzionale scheduler e le sue interfacce IRQ/timer, non una review dell'intero OS.

## Risultato e perimetro

La task consumer attende l'evento 2; il worker arma il LAPIC one-shot calibrato
ed esegue un compute Assembly con IF=1. Un IRQ hardware 0x41 interrompe quel
worker sul suo stack privato. L'entry esistente salva GPR e x87/SSE, valida il
frame e l'owner, entra nel callback C con IF=0 e torna mediante IRETQ.

Il driver ferma la source e completa EOI prima di pubblicare la notifica.
La pubblicazione Rust usa un AtomicU32 separato dall'Owner dello scheduler:
nessun borrow dell'Owner, allocazione o log nel gestore. Il dispatcher,
dopo che il worker ha terminato il RUNNING corrente, applica una transazione
Rust e risveglia il consumer. Tre dispatch, sei attraversamenti del contesto,
due stack raccolti, budget ripristinato e 144 PTE task assenti a fine profilo.
Il precedente carico di attesa/yield esegue ancora i suoi 210 dispatch.

Si ammette il solo vettore LAPIC 0x41 nella sezione compute del worker.
Le funzioni C delle task, switch, wait, mapping, raccolta e gateway restano IF=0.
Non e ancora una politica generale per IRQ arbitrari in qualunque codice task,
ne uno scheduler preemptive. Capacita otto slot, BSP, task kernel fidate,
x87/SSE; niente AP/SMP, AVX/AMX, user mode o test su hardware fisico.

## Correzioni emerse dai test

1. Il vecchio frame-owner accettava solo lo stack principale. Ora l'autorita
   della task corrente, generazione, mapping e lease pinned precedono la lettura
   del frame fuori dallo stack principale; RIP/RSP/IF e source sono poi validati.
2. L'integrazione iniziale armava il timer prima di preparare gli stack. Una
   ripetizione ha raggiunto TASK_IRQ_OK ma fallito TIMER_DEADLINES_FAILED: il
   vecchio intervallo PM comprendeva preparazione/raccolta e attesa HPET.
   Il worker ora arma dopo la preparazione, registra arm/compute separatamente;
   il limite esistente non e stato ampliato per nascondere il fallimento.
3. La prima verifica del percorso negativo richiamava un validatore riservato
   al dispatcher dallo stack task e veniva correttamente rifiutata. Il controllo
   terminale usa ora verifiche di lease/pin/mapping prive di mutazioni e adatte
   al contesto task. La prova negativa successiva verifica le pagine trattenute.
4. L'host verifier richiede il marker IRQ e i dati del frame; un semplice
   KERNEL_READY non basta. In caso di errore tenta anche l'acquisizione dello
   stato CPU prima di terminare esclusivamente la VM posseduta.
5. Cppcheck rilevava un cast diretto del puntatore sentinella nel test negativo
   sleep del driver: ora il passaggio di larghezza e esplicito BOOT_U64.

Gli avvii falliti/intermedi restano in build e non sono conteggiati come PASS
della modifica finale. Nessun retry automatico per occultare un caso fallito.

## Host e analisi statica

| Prova | Risultato | Perimetro |
| --- | --- | --- |
| Core Rust debug/release | 22/22 per profilo | Errori, epoche, overflow transazionale, coda, stati, generazioni |
| Pending atomico | Bounds, coalescing, restore e concorrenti passano | Otto thread host; non certifica un kernel SMP |
| FFI C/Rust | 496699 controlli passano | ABI, Busy/Denied, 10000 post coalescenti, rifiuto Running conserva bit, risveglio |
| Clippy/rustc/MSVC/MASM | Warning trattati come errori, build passa | ABI Microsoft x64 supportata |
| CPU/context host | 55900 controlli × 10 | Snapshot privilegiati mock; vero rifiuto CPL3 prima di IN/CLI |
| Context switch host | 4004 switch × 10; quattro guard fault attesi/run | GPR/XMM/FP/canary; due reservation Windows rilasciate/run |
| Stack resources | 361000 controlli × 10 | Allocator/PTE mock, rollback, retry e accounting |
| Native mapper | 554149 controlli × 10 | CPU/PTE mock, bounds, proprieta, invalidazione |
| Ponte IRQ | 22 controlli × 10 | Backend/owner mock; source/frame/IF/DF, doppia notifica, binding conservato |
| LAPIC ASan regressione | 10 ripetizioni, 10000 event cycle | Driver host senza macro integrazione; non prova IRQ hardware |
| Cppcheck scheduler | 14 input; zero error/warning/performance/portability | Style/information disponibili nel report XML |
| Cppcheck interfacce | Tre input con KERNEL_SCHEDULER_IRQ=1, zero diagnostiche bloccanti | IRQ return, driver LAPIC e runtime |

ASan strumenta fixture C FFI, CPU/context, stack resources, mapper, bridge e
driver host indicati; Rust staticlib e Assembly non sono strumentati da ASan.
Gli switch host su stack privati sono provati separatamente senza ASan fibers.
Il core/trasporto Rust non usa heap. Accounting native dimostra restituzione
delle lease dei due stack nel profilo positivo; non dimostra zero leak di tutta
la RAM del kernel. Valgrind non eseguito; coverage linee/branch N/D.

Evidenze host sotto `kernel/build/`:

- `scheduler-host-0722ec972a4b44f7811454ead558d754/`: Rust, Clippy, FFI ASan, modello.
- `scheduler-context-6aa4d362f8204d81a0df2a2ebe671252/`: ultimo backend/bridge,
  disassembly e cppcheck; `sources.json` documenta lo stato provato.
- `lapic-asan-b58c087d080243c7b4e5b441c72766a4/`: regressione finale timer host.
- `scheduler-irq-interface-review/`: XML interfacce, manifest sorgenti e righe.

## Firmware sull'immagine finale

Immagine positiva: `kernel/build/scheduler-irq-validated/kernel.elf`.
SHA256: `23C24094FD4ED7CCFFFC135E0DCB313FA09793C3A545943C2EA01CACAD9FB2CE`.
Dieci boot consecutivi PASS:
`kernel/build/scheduler-firmware-95cd2a55fe2b46dcb95580a593a33735/`.

QEMU q35/TCG, CPU max, 128 MiB, un BSP; OVMF e ESP privati, hash dell'immagine
copiata controllato. Il verifier confronta marker, RIP nel compute, RSP nello
stack worker, indice 1/generazione almeno 2, IF9=1/DF10=0, tre dispatch e budget.
Al termine verifica CR3 invariato, RSP sullo stack principale, RAX ready,
144 PTE task a zero e assenza di triple fault. I canary compute verificano
GPR e tutte le sedici XMM/immagine x87 dopo il ritorno IRQ, prima di completare.
Il normale driver conferma consume, tre consegne, stop/unbind e cleanup HPET.
Un controllo finale delle dieci trace CPU correla il primo IRQ hardware 0x41
al RIP worker registrato e IF=1; le tre consegne hardware sono presenti in ogni
boot. Evidenza aggiuntiva: `hardware-irq-evidence.json` nella cartella positiva.

La prova `irq-timeout` ferma volutamente la source tramite l'API driver,
poi esegue il compute con IF=1 fino alla deadline PM/ceiling di iterazioni.
IF torna a zero; il profilo termina col marker atteso e RAX terminale. Due
lease pinned, 32 pagine dati e guardie assenti restano conservate: non e un
cleanup completato e non e un leak nascosto. La deadline usa PM ACPI; HPET e
usato nel successivo idle/HLT driver, non come watchdog armato nel compute.
PASS: `kernel/build/scheduler-firmware-a5c7d6298eb340c888bad9379a7926bf/`.

Regressioni finali, una esecuzione per immagine isolata, tutte PASS:

| Profilo | Evidenza sotto kernel/build/ | Riscontro |
| --- | --- | --- |
| guard-low | scheduler-firmware-cb4fcbed551a4651b5255a0af675f754 | #PF guardia bassa, errore 0; stack vivi conservati |
| guard-high | scheduler-firmware-da1d5717597c45a2911600e8ccc73a0e | #PF guardia alta, errore 0 |
| nx | scheduler-firmware-b8f38c4581ff4e1d8091ecb2d0ec2d6b | #PF execute sullo stack, errore 17 |
| retired | scheduler-firmware-e7ac2ead79474829b4d582cdf4096978 | #PF dopo revoke; tutte le PTE assenti |
| rollback | scheduler-firmware-0796e8c284fa4184a594a4e613f3a2ad | Rilascio/abort e unico retry intenzionale; profilo IRQ termina |
| blocked | scheduler-firmware-cd22ea4d523c418f9f35b31d261c4cd7 | Due WAITING conservati; terminale diagnostico distinto da EMPTY |
| irq-timeout | scheduler-firmware-a5c7d6298eb340c888bad9379a7926bf | Source fermata; compute bounded, IF chiuso, lease conservate |

Ciclo continuo: `kernel/build/loop-firmware-9d203e6a552541d794de55ed34c057b0/`,
dieci eventi hardware, stop esterno e cleanup verificati. Il primo IRQ arriva
nel compute worker; gli altri ritornano al sonno IF=1. Il vecchio verifier
richiedeva tutti gli IRQ durante HLT ed e stato aggiornato con questa distinzione
esatta, senza ammettere RIP arbitrari. CPU/LAPIC, owner timer/controllo e owner
HPET sono verificati dopo lo stop. La connessione debugger resta locale alla VM
posseduta. Un tentativo nel sandbox non ha potuto aprire il socket locale;
la prova eseguita col permesso necessario e quella PASS qui elencata.

## Review, limiti e prossima fase

Massimo sorgente scheduler: 353 righe (`integration/kernel.c`); massimo cinque
file diretti nelle cartelle funzionali. `kernel/main.c`, gia a 400 righe,
non e stato modificato da questo blocco. Nessun renaming arch/x86, nuova ESP
principale installata, commit/push o modifica del preesistente address_ranges.c.

Fonti Assembly: Intel SDM093 Vol.3A §7.8.1, IF bit9/STI/CLI; Vol.2A pp.3-148,
3-354, 3-409..419 Table3-48 e 3-455. Clock/port e accessi timer provengono dalle
interfacce dei driver validati; nessun nuovo offset MMIO assunto.

Gli atomici riguardano le notifiche, non la durata dei payload o la gestione
dei driver su SMP. Le validazioni host di frame/source usano mock; l'ownership
del vero frame IRQ su stack task e provata dal profilo QEMU positivo, non da
una copertura completa di tutti i rifiuti del codice IDT. Mancano prove fisiche,
stress con piu source concorrenti, IRQ generici nelle task C e recupero/restart
dopo il terminale timeout; non sono dichiarati implementati da questo blocco.

Review finale: blocco v0 validato per il perimetro dichiarato, nessun test finale
fallito. Dieci boot positivi, sette scenari negativi e un ciclo continuo; nessuna
modifica al codice C/Rust/Assembly dopo queste prove. I report precedenti restano storici.

Prima di driver ulteriori: definire binding per source/evento e condizioni
persistenti, politica dei vettori ammessi e intervalli IF=1, e prove di
quiescenza/lifetime. Preemption e SMP richiedono un progetto distinto.
