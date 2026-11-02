# K3-I — AP nel boot ordinario

## Risultato

Il BSP prosegue fino al normale `KERNEL_READY` dopo aver avviato e
parcheggiato tutti e tre gli AP QEMU. La stessa immagine funziona con una
sola CPU. Gli AP mantengono IF=0 e non eseguono task: lo scheduler è BSP.
Scelta e limiti sono nel [design](k3i-runtime-design.md).

Il nuovo owner persistente orchestra installazione, commit congiunto,
preparazione panic, pubblicazione e chiusura del gruppo, avvio e riarmo.
Prima della barriera cancella l'immagine e rilascia le risorse di transizione
ancora revocabili; dopo la barriera le trattiene e ogni errore è terminale.
Il profilo ordinario non linka i probe AP né il terminale Assembly dei test.
I precedenti profili transizione/commit/panic e bootstrap memoria rimangono
isolati dal collegamento ordinario.

## Checkpoint integrazione nel boot ordinario

| Prova | Esito | Evidenza sotto `kernel/build/` |
|---|---|---|
| Ordinario, quattro CPU | 10/10 | `ap-runtime-5e3b418713cb412cba82d24bac61b55b/results.json` |
| Stessa immagine, una CPU | 10/10 | `ap-runtime-28e9fa38ab634e648131a25f27b9563a/results.json` |
| Panic dopo timer e scheduler BSP, tre ACK | 10/10 | `ap-runtime-19afeafbd0394f9b93acab3131d2529d/results.json` |
| Console GOP interattiva, quattro CPU | 10/10 | `pointer-interactive-check-deccf73f98664f2896cf5512b0add9d2/results.json` |
| Regressione diagnostico sequenziale | 10/10 | `first-ap-firmware-4381b159da544e758150ff59edf2c893/results.json` |
| Compatibilità runner per-CPU precedente | 1/1 | `percpu-firmware-14e760e444be492a96c551a2239e30cd/results.json` |
| Orchestrazione C/ASan, dipendenze modellate | Passata | `test-run-f30bcdadb732416b873b5af320c3bc36/ap-runtime.log` |
| Trasporto C/ASan e Rust reali, I/O modellato | 9 + 22 scenari e 2 test monitor passati | `k3i-runtime-native-host/` |
| Audit emergenza immagine ordinaria | 11 oggetti per immagine senza FP/SIMD rilevati | `k3i-runtime-release-{ordinary,console,panic}/*.asm.txt` |
| Cppcheck dei sorgenti coinvolti | Nessuna diagnostica bloccante | `ap-runtime-host-46a6678105fd404988f16f32ed4dddb9/cppcheck.xml` |

Le cinque campagne da dieci sono distinte dalle prove preliminari.
Il monitor controlla registri reali, stack privati, GS/GDT/TR/IDT, NX/WP/IF,
frame PARKED, codice/tabelle di transizione immutabili, pinning, guardie e
conservazione delle pagine libere e delle lease vive senza sovrapposizioni.
I report distinguono CPU inizializzate, AP parcheggiati e CPU schedulabili.

La console prova movimento, pixel, Esc, `help`, `exit` e cleanup. Dopo la
chiusura verifica di nuovo AP, contesti e accounting. Nel panic il motivo
deve essere proprio `INTENTIONAL_TEST_PANIC`: un errore imprevisto del
kernel non può passare grazie al solo marcatore finale di arresto.
Il reporter BSP e i tre follower raggiungono HLT con marcatori distinti,
quattro record completi e nessun ACK mancante.

L'host esercita 19 punti di errore dell'orchestrazione, successo con 1/4/64
identità, rifiuto di chiamate duplicate, lock posseduto, input nulli e limiti
invalidi, oltre all'arresto se fallisce il rollback. I/O e ammissione sono
modellati in questa suite; le campagne firmware dimostrano l'esecuzione AP.

## Verifiche e utilizzo

`scripts/test/run.ps1 -Suite ap-runtime` esegue la nuova suite host.
`scripts/test/campaigns.json` registra `ap-runtime`, `ap-runtime-bsp` e
`ap-runtime-panic`. Il build ordinario non richiede un'opzione SMP.
La console usa i consueti flag Ps2Input/Ps2Mouse/ConsoleShell/InteractiveInput.

Prima compilazione fermata da /WX per una dipendenza mancante del nuovo
header: aggiunto l'include esplicito del tipo LAPIC. Prima analisi statica
dei test fermata per chiamate con effetti dentro assert: separate senza
disattivare i controlli. Cppcheck conserva informazioni sugli include e
suggerimenti const nei mock con firma imposta dall'API.

Il controllo strutturale globale applicava ancora 400 righe e cinque file,
in contrasto con le regole locali aggiornate e con due test preesistenti
da 490/413 righe. Dopo scelta esplicita dell'utente, allineato a 1.000/8;
restano attivi controllo dei percorsi e dei riferimenti. I sorgenti di
questo blocco restano tutti entro 400 righe.

La campagna host globale ha passato 50/51 suite; l'unico errore residuo era
il gate indirizzi K1 su due limiti fisici di 4 GiB già presenti nel modulo.
Il trampoline carica CR3 tramite EAX (`entry.asm`, tratto BITS 32): il limite
è ora espresso con `UINT32_MAX + 1` in C e `u32::MAX as u64 + 1` in Rust.
Il valore resta identico; nessuna esclusione aggiunta al checker. Ripetuti
con successo gate layout e suite transizione, più suite runtime aggiornata
con rifiuto IF=1, CPU diversa dal BSP e owner incompleti.
Evidenze: `test-run-eaf4a4eb95554eb1adb08676f4428abb/`,
`test-run-19bc7aa7423f4830b5054221b5e0bb44/`,
`test-run-d6f50802c94d412588b6a7b9d47343de/` e
`test-run-f30bcdadb732416b873b5af320c3bc36/`.

La revisione finale anticipa il rifiuto di CPU non BSP o IF=1 prima delle
letture dello stato condiviso. Dopo questa modifica sono ricompilate e
ricollaudate tutte le immagini ordinarie interessate; il diagnostico
sequenziale non linka il nuovo orchestratore.

La build senza flag usa ora correttamente `build: []` nel registro campagne;
`build: null` resta riservato ai runner che costruiscono da soli. Il comando
centralizzato QEMU passa il proprio smoke: `qemu-run-ac9ec387641c411db12be32354a2e7ad/`.
Il primo tentativo usava PowerShell 5.1 ed è stato rifiutato come previsto;
il comando supportato richiede PowerShell 7.

Pillow 13.0.0 serve al confronto delle immagini: installato soltanto in
`kernel/build/runtime-python/`, senza modificare l'interprete globale.
Si usa Python 3.12 esplicito; l'alias WindowsApps non è utilizzabile qui.

## Memoria, fonti e limiti

Nessuna nuova allocazione dinamica nell'orchestratore; owner statici e
risorse CPU già previste dal contratto. ASan riguarda il C host, non Rust,
Assembly o RAM guest. Accounting guest non equivale a un leak detector
fisico. Valgrind, TSan, coverage linee/branch e hardware fisico N/D.

[Intel SDM 253668-078US](https://cdrdv2-public.intel.com/671190/253668-sdm-vol-3a.pdf):
§9.4.4.1, §11.6.1 e §6.7.1. Nessun nuovo offset hardware o codice Assembly.
Restano i limiti del protocollo iniziale IVT/NMI e del profilo xAPIC QEMU.
La campagna interattiva qui è GOP, non una nuova certificazione VirtIO/VT-d.

Quel checkpoint precede lo stress finale riportato sotto. Manifest
immagini/sorgenti dell'integrazione:
`kernel/build/k3i-runtime-manifest.json`.

## Chiusura K3

2 novembre 2026: base K3 chiusa sul profilo xAPIC QEMU/OVMF, con BSP e tre
AP reali nel kernel. Scheduler e dispositivi ordinari restano sul BSP;
gli AP sono parcheggiati e partecipano al protocollo panic.

| Campagna finale | Esito | Evidenza sotto `kernel/build/` |
|---|---|---|
| NMI esterno BSP e arresto remoto | 10/10 | `ap-panic-stress-035cbe67c820486182ea2fb0b56884f9/results.json` |
| Quattro origini NMI prima dell'elezione | 10/10 | `ap-panic-stress-5d8312da66bc462cb1cdce7beca2c8dc/results.json` |
| ACK intenzionalmente omesso | 10/10 | `ap-panic-stress-122bb1c97cbe4650a071959ae17a5fe4/results.json` |
| UART con DLAB alterato, rapporto in RAM | 10/10 | `ap-panic-stress-849e150481204c8d91786a024974fd0e/results.json` |
| Ticket lock reale trattenuto e NMI ricorsivo | 10/10 | `ap-panic-stress-ccdc0ae534054cba8adb47ffa6b87289/results.json` |
| Boot ordinario, quattro CPU | 10/10 | `ap-runtime-8794f9bc64a24779a47a21fc0028f364/results.json` |
| Stessa immagine, solo BSP | 10/10 | `ap-runtime-2cbda29563254e7094c28e900c43a2fe/results.json` |
| Console GOP, input/pixel/cleanup e AP | 10/10 | `pointer-interactive-check-e63db3a956ca40a7b378f89f47f1e67f/results.json` |
| Sessione continua da cinque minuti | 1/1 | `pointer-interactive-check-a77e2c6c015943298a18d4d537b75631/results.json` |

Gli 80 collaudi distinguono 30 boot ordinari/console dai 50 panic voluti.
Ogni scenario complesso usa dieci boot consecutivi sull'immagine finale,
con ESP privata e hash controllati. Il soak separato dura 300,953 secondi:
404 coppie di movimento, 808 pacchetti hardware confermati e spostamento
netto nullo, poi pixel, Esc, `help`, `exit` e verifica di contesti/AP/lease.
Nessuna perdita input o variazione inattesa del budget osservata.

Il rendezvous diagnostico usa atomiche Rust e quattro catture complete
prima dell'elezione. I vincitori effettivi sono BSP (1 prova), AP1 (2),
AP2 (3), AP3 (4). Un solo reporter, tre ACK e missing=outcome=0.
L'ACK omesso produce missing=2 e un'attesa limitata; il core è realmente
arrestato, ma non pubblica la conferma. Il caso UART rileva DLAB e salva
output=2 in RAM. Non riproducono SMM, CPU fisicamente bloccata o UART
guasta: la condizione THRE bloccata resta coperta dal modello host.

Nel test lock-held il monitor verifica lock, ticket, guardia e profondità
reali nel contesto BSP. Il motivo UINTPTR_MAX resta dato numerico.
Un NMI successivo porta il reporter all'arresto ricorsivo senza alterare
il primo rapporto; gli altri core rimangono arrestati. Registri, stack/IST,
CR3/NX/WP/IF, descrittori, record, pinning e accounting sono verificati.

Sette suite host passate: panic-stress, panic, ap-native, ap-runtime,
percpu-clock, sync e layout. Il nuovo rendezvous esegue 20 processi con
quattro thread e quattro negativi C/ASan, con atomiche Rust reali e clock
modellato. Tre test Python coprono il polling, inclusa la race corretta.
Analisi statica senza diagnostiche bloccanti; 57 oggetti d'emergenza
disassemblati sulle cinque immagini, inclusi i due helper diagnostici.
I risultati e gli hash sono nel manifest `kernel/build/k3-close-manifest.json`.

### Debug e limiti della prova

Il primo tentativo conteso è fallito: il comando NMI di QEMU passa per
LINT1, mascherato sugli AP. Corretto il solo innesco diagnostico usando
IPI fisiche ai singoli ID, AP prima e BSP ultimo. Il protocollo ordinario
resta quello approvato. Fonti e offset nel design e nel sorgente trigger.

Una prima campagna contesa è stata interrotta a 3/10 dal lettore: due
snapshot seriali separati potevano vedere prima l'assenza del marker e poi
il panic già previsto. Il log conservato mostra il corretto arresto guest.
Ora un solo snapshot governa entrambe le decisioni; aggiunta la regressione
e ripetuta da zero la campagna. Nessun risultato preliminare fallito viene
sommato ai dieci finali. /WX ha anche rifiutato una collocazione iniziale
del probe lock-held che rendeva irraggiungibile il terminale ordinario;
spostato nel ramo diagnostico corretto, senza sopprimere warning.

I nuovi hook e i rendezvous sono compilati soltanto nei relativi profili
di test. Le campagne sono registrate come `k3-*`; il soak è `k3-soak`.
La suite host è `scripts/test/run.ps1 -Suite percpu-panic-stress`.

La chiusura riguarda inizializzazione e parcheggio AP, contesti per CPU e
panic coordinato su QEMU. Non certifica scheduler SMP, hotplug, userspace,
hardware fisico o NMI prima della IVT iniziale. ASan copre il C host;
Rust, Assembly e RAM guest non sono strumentati. Accounting e handles
chiusi sono distinti da un leak detector. Valgrind, TSan e coverage N/D.
