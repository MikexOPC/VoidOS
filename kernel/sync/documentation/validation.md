# K2-A — primitive verificate il 27 ottobre 2026

Stato: primitive e binding verificati, ma il gate di integrazione resta
aperto per il panic timer intermittente descritto sotto. Non avanzare
all'adozione nella memoria dichiarando risolto quel panic. **K2 nel suo
insieme non è completato:** allocatore, heap e lettori esterni non sono
ancora protetti dai nuovi lock. Dal 28 ottobre il kernel ordinario collega
il runtime e inizializza il contesto BSP prima della memoria; il probe
TestKernelSync resta separato. [Prove](../../mm/physical/documentation/k2-core-validation.md).

## Implementazione

- C: lock e contesto senza allocazioni, guard legati al proprio indirizzo,
  token monotoni, ranghi crescenti, massimo otto livelli e release LIFO.
- Rust 1.98.1: AtomicU64 con load-acquire/store-release; CAS di Next relaxed,
  sufficiente per riservare il ticket. Il payload dipende dalla coppia
  release/acquire su Serving. Nessun accesso misto di larghezze diverse.
- Gateway FFI unsafe ristretto; il chiamante deve garantire memoria RW
  viva/allineata e accessi atomici. Non rende sicuro un puntatore arbitrario.
- Backend x86: IF reale salvato e disabilitato; restore del solo IF; PAUSE.
  L'ordine dei lock non viene affidato a `volatile` o al solo IF=0.
- Prima della prenotazione un fallimento non consuma ticket. Dopo la
  prenotazione clock guasto/scaduto/fermo o contabilità invalida sono
  terminali. Il callback del clock non può acquisire lock né abilitare IRQ.
  I contatori non fanno wrap: esaurimento rifiutato prima di prenotare.

## Prove finali

Evidenze sotto `kernel/build/`; gli artefatti non vengono versionati.

| Prova | Esito | Evidenza |
|---|---|---|
| Rust debug/release | 3/3 in entrambi i modi: policy, FFI e CAS concorrente; Clippy senza warning | `test-run-06e6010c98344fdda1ebaa831ae37ac3/sync.log` |
| C ASan + MSVC /W4 /WX | Init, IF, rank inversion, recursion, LIFO, copie/token stantii, alias, esaurimento, clock wrap e handoff | `sync-host-73712d2efbf64739ae8b3c9f8259a19b/` |
| Contesa host reale | 2/4/8 thread, 20.000/40.000/80.000 acquisizioni FIFO; payload di 16 parole e annidamento verificati | stesso report host |
| Fail-stop host | 7 processi distinti, exit 86 previsto: deadline, clock fermo/guasto/assente, callback con IF errato, release/nesting con IRQ attivi; ticket non cancellato | stesso report host |
| Analisi statica | cppcheck senza diagnostiche error/warning/performance/portability | `sync-host-73712d2efbf64739ae8b3c9f8259a19b/cppcheck.xml` |
| Kernel positivo | 10/10 consecutivi, 128/256 MiB: IF0/IF1 reali, annidamento, guard rifiutati, handoff esplicitamente simulato, KERNEL_READY | `sync-firmware-8abf6d374cae471988ff2b9b7f63d3b5/results.json` |
| Kernel negativo attesa | 1/1, LOCK_WAIT_FAILED dopo arming; nessun KERNEL_READY successivo | `sync-firmware-93aa6447a94d4618aea7f8fd4b08a795/results.json` |
| Kernel negativo IRQ | 1/1, LOCK_RELEASE_IRQ_ENABLED dopo arming; nessun KERNEL_READY successivo | `sync-firmware-acc10d6822534eb2aebffe4b1c439534/results.json` |
| Regressione ordinaria | 10/10 consecutivi senza marker del probe; K1 resta valido | `sync-firmware-736b95679e24457fb4af7ff8f67c2cfe/results.json` |
| Runner dopo spostamento in scripts/ | 1/1 positivo, stesso codice del probe | `sync-firmware-2fc59a4089c3424593eef27be97b5b10/results.json` |

Il riepilogo delle tre campagne diagnostiche è
`qemu-run-124953b8355542c9bad92ea200f54112/results.json` (3/3, exit code 0).
Gli hash delle immagini e del loader sono nei report firmware.

## Review e limiti

La revisione ha aggiunto rifiuto dell'alias guard/clock, controllo del rango
immutabile al release e rilevamento del callback che abilita IRQ. ASan,
contesa e negativi sono stati rieseguiti dopo tali modifiche.

Quattro sorgenti diretti nel modulo, massimo 163 righe per sorgente di
produzione; massimo cinque file nelle cartelle funzionali. Controllati
spazi, indentazione, separatori, link e diff. Le fonti hardware e i
prerequisiti sono nel [contratto](design.md).

Nessuna allocazione nella primitiva; ogni handle di thread/evento creato
dal test viene chiuso con esito verificato e accounting proprio a zero.
Il conteggio globale del processo resta diagnostico: vedere il riscontro
qui sotto. ASan strumenta il C host, non tutte le
operazioni Rust né la RAM fisica del kernel. Valgrind e ThreadSanitizer
non sono stati eseguiti. Coverage di linee/branch non misurata.

La contesa multithread è reale sull'host; la VM usa solo il BSP. Non sono
avviati gli AP e non viene attestato SMP guest. NMI/fault non possono
usare questi lock; niente yield/sleep mentre sono detenuti. Il contesto
deve restare esclusivo della CPU chiamante: GS/per-CPU e controllo
dei lettori della memoria rimangono passi successivi, non garanzie già
fornite da questo modulo. Nessuna prova su hardware fisico.

Nessun commit/push in questo blocco.

## K2-B: binding BSP e collegamento del clock

L'accessor scelto dall'utente è implementato nel profilo isolato.
Rifiuta l'accesso prima dell'init, l'init ripetuta e gli AP non predisposti.
Controlla CPUID/MSR prima di accedere allo stato C condiviso del BSP;
nessun GS/per-CPU o avvio AP viene dichiarato implementato.

Il collegamento del clock richiede contesto valido senza lock detenuti,
descrittore vivo non sovrapposto/allineato e budget valido. I test host
coprono clock nullo, ticks/poll nulli, ticks oltre metà periodo, contesto
copiato, alias, disallineamento, rifiuto sotto lock e preservazione IF0/IF1.
La selezione BSP/AP nei test host è simulata; CPUID/MSR e IF reali sono
eseguiti nel guest. Il probe usa il contesto restituito dall'accessor per
annidare e rilasciare lock nei due stati IF, senza riacquisizione implicita.

Host finale: `test-run-1b95fb4e358b41f4b1202e5d8ddd47d5/sync.log`,
con ASan, Rust debug/release 3/3, Clippy, cppcheck esteso al provider,
140.000 acquisizioni e sette fail-stop attesi.

La review ha corretto un errore di confine: init/bind del clock accettavano
Ticks pari a Mask/2, mentre `kernel_deadline_start` li rifiuta. Ora entrambe
le API richiedono Ticks strettamente inferiori; testano Mask/2-1, Mask/2 e
Mask/2+1. Questo fix non viene presentato come causa del panic LAPIC.

### Correzione del controllo handle intermittente

La prima riesecuzione ha fallito il confronto del numero globale di
handle (`test-run-a002cbee316c47c4ae64cb7f15e40c66/`); non è un PASS.
Una riproduzione con il conteggio esplicito è conservata in
`k2-sync-handle-reproduction.json`. Il controllo separato
`k2-handle-control.c`/`.log`, privo di codice kernel, riproduce l'aumento
di uno anche chiudendo tutti gli otto handle creati (20 processi).
Non viene attribuita al kernel una perdita sulla base di quel conteggio,
né viene certificata l'assenza di perdite del runtime host.

Il test ora verifica ciascun Create/Close posseduto, azzera gli handle
rilasciati e richiede accounting proprio a zero; registra anche il numero
globale senza usarlo come contatore delle risorse possedute dal modulo.
Dopo la correzione: 30/30 processi host, 2/4/8 thread ripetuti dieci volte,
1.400.000 acquisizioni FIFO, nessun fallimento o diagnostica ASan
(`k2-sync-handles-final-stress.json`). Non sono 30 boot QEMU.

L'allocatore fisico e l'heap non adottano ancora i lock: il prossimo
blocco deve includere anche i lettori esterni prima di attestare sicurezza
concorrente della memoria. La VM rimane BSP e non prova SMP guest.

### Gate QEMU e panic timer ancora aperto

La campagna `qemu-run-1475075e3dd64d6a973e89d362567698` ha chiuso 2/3:
due fail-stop previsti passano, il positivo chiude 9/10. Nel decimo boot
il probe dei lock termina con tutti i marker, poi il test LAPIC produce
TIMER_DEADLINES_FAILED e LAPIC_TIMER_INVALID. Watchdog Failure/Cleanup=0,
Result=EVENT, cleanup riuscito. I log non identificano la condizione
specifica: nessuna attribuzione certa al renderer, ai lock o al carico host.

Con estensione del perimetro autorizzata dall'utente, sono state aggiunte
diagnostiche di goal, step, valore osservato, count, phase e consumed.
Nessuna soglia allargata, retry del timer o soppressione del panic.
Seguono 10/10 boot (`sync-firmware-0e64e7ef8e2f4a2c846bf2e7610da199/`)
e 20/20 della stessa immagine (`sync-firmware-08b09deaae29459ca2ec81beba1a9b51/`).
Il panic non ricompare; questi successi non costituiscono un fix né
dimostrano assenza di flakiness. L'indagine resta aperta, senza avanzare
all'allocatore. [Dettagli timer](../../../docs/validation/consolidamento.md#validazione-dei-backend-x86).

Negativi finali ricompilati dopo il fix deadline e la diagnostica timer:
`qemu-run-add5c6237ada4811bef77de77e128591/results.json`, 2/2. Osservati
LOCK_WAIT_FAILED e LOCK_RELEASE_IRQ_ENABLED soltanto dopo l'arming
previsto, senza prosecuzione a KERNEL_READY. Lo stress host 30/30 sopra
è stato rieseguito anche sul binario con il fix deadline.

Manifest/review finale: `k2-sync-binding-review.json`, nessuna violazione
nel perimetro, massimo 298 righe includendo driver.c del timer e cinque
file diretti per cartella funzionale. Il diff globale contiene spazi
finali preesistenti in kernel/main.c e kernel/startup/startup.h, fuori dal
blocco: non sono stati rimossi silenziosamente dalle modifiche dell'utente.

Suite host kernel passata (`test-run-679c9554be4e488e988ede1300f3a6b6/`),
inclusi nove test Rust LAPIC e i test C del modulo. LAPIC ASan: dieci
ripetizioni/10.000 cicli eventi, nessuna diagnostica
(`lapic-asan-ec23ae7e439d468ea415c3b3f7fa6add/`). I test host usano mock
del watchdog e non riproducono il percorso completo delle scadenze guest.

Aggiornamento successivo, scelta utente: implementata e verificata la
separazione arm→IRQ/ritorno watchdog, senza cambiare soglie. Regressione
mock del falso positivo riprodotta; IRQ tardivo resta rifiutato. Passano
dieci boot probe, dieci ordinari, ciclo continuo e negativo IRQ perso.
Il [report timer](../../../docs/validation/consolidamento.md#validazione-dei-backend-x86)
distingue questo fix dalla causa non catturata del panic originale.


## Adottato dal pool fisico, 28 ottobre

Il runtime è ora usato anche da `kernel/mm/physical/allocator.c`: un lock
per owner, rango foglia 300; nessuna riacquisizione nel core privato.
I lettori nei chiamanti usano query piccole sincronizzate, inclusa la
generazione usata dal renderer. Contesa su 2/4/8 thread Windows con contesto
CPU TLS, 14.000 cicli lease complessivi e accounting ripristinato; non
costituisce SMP guest. La regressione host verificata comprende 39 suite
in aggregato, con tre adattatori mock corretti e ripetuti. Tutti i dettagli,
le immagini finali e i limiti di ASan/accounting sono nel
[report pool](../../mm/physical/documentation/k2-pool-validation.md).
Heap, holdings e lifecycle dei mapper non sono dichiarati concorrenti.


## Successivo blocco K2-C: heap

Il nucleo heap usa ora il lock per owner con rango 200 e il medesimo
accessor CPU. Azzeramento e poison sono fuori dal lock su memoria
riservata fino al commit; nessun callback sotto il guard. Verifiche:
200.000 operazioni casuali ASan, 14.000 cicli su 2/4/8 thread host,
40 trasferimenti sospesi, dieci boot del diagnostico e un boot interattivo
GOP. [Prove heap](../../mm/heap/documentation/k2-validation.md).
Il gate finale è poi superato il 29 ottobre: [chiusura K2](k2-integration.md).
Le note precedenti sono cronologiche e conservano i limiti dei rispettivi blocchi.
