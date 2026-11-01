# K3-I — prerequisiti del riarmo sequenziale

## Stato

Verifica delle tabelle dopo il primo AP implementata e collaudata.
Il canale di riscrittura della pagina dati e gli altri AP non sono ancora
collegati. Il contratto di riarmo è stato approvato nel
[design](k3i-sequential-design.md); K3 resta aperto.

`xt_verify_live_tables` richiede risorse congelate e mantiene ownership,
pinning, root e permessi. Ammette Accessed nelle voci presenti e Dirty
soltanto nelle foglie; nelle voci vuote non ammette variazioni.
`xt_verify_tables` conserva il confronto esatto usato prima dell'avvio.
Nessuna scrittura nelle tabelle e nessuna nuova allocazione.

Fonte: [Intel SDM 253668-078US, §4.8](https://cdrdv2-public.intel.com/671190/253668-sdm-vol-3a.pdf),
Accessed bit 5 nelle voci usate per la traduzione e Dirty bit 6 nelle
voci che mappano pagine. Il confronto live non autorizza da solo il riarmo.

## Prove finali

- Host C con ASan, /W4 /WX; Rust 2 test debug e 2 release, -D warnings.
  Suite originale di lifecycle/rollback e 13 failure point, più congelamento,
  null/stati iniziali, mutazione dei singoli bit nelle quattro tabelle,
  voci vuote e cinque failure point di lettura. Tutti passati.
  Evidenza: `kernel/build/k3i-live-tables-host-final/` e relativo log.
- Regressione host joint: positivo e 52 scenari di errore/retention passati,
  con la verifica inattiva ancora rigorosa.
  Evidenza: `kernel/build/k3i-live-tables-joint-host/`.
- QEMU/OVMF: 10/10 boot consecutivi del primo AP reale; il probe verifica
  le tabelle dopo PARKED e pubblica `KERNEL_AP_LIVE_TABLES_VERIFIED`.
  Evidenza: `kernel/build/first-ap-firmware-3587d31d5a04452698b3b9ff6c829623/`.
- Regressione ordinaria: 10/10 boot, AP fermi.
  Evidenza: `kernel/build/percpu-firmware-a193a7be97ee45e3b99da44a810ebbed/`.
- Cppcheck nelle due suite host senza diagnostiche bloccanti; informazioni
  sugli include e suggerimenti di stile conservati negli XML.

Il primo tentativo host è stato fermato da /WX per una variabile del test
che nascondeva un parametro: il blocco di test era stato inserito nel caso
inattivo invece che nel caso congelato. Corretto prima delle prove finali;
log iniziale conservato, non contato come PASS.

Il monitor firmware verifica anche registri AP, lease, guardie e budget.
ASan riguarda il C host; RAM guest, Rust e Assembly non sono strumentati.
Coverage, Valgrind, TSan e hardware fisico N/D. Nessuna prova del secondo
AP o del riarmo viene dedotta da queste campagne.

Manifest immagini, marker e sorgenti:
`kernel/build/k3i-sequential-live-manifest.json`.
# K3-I — avvio sequenziale del gruppo AP

## Risultato

Il diagnostico avvia in sequenza tutti e tre gli AP QEMU: **quattro core
inizializzati e parcheggiati nel kernel**, con contesti privati verificati.
Il canale di riarmo aggiorna la sola pagina dati dopo PARKED; codice,
tabelle e lease rimangono trattenuti. Il [contratto approvato](k3i-sequential-design.md)
è implementato. Il boot ordinario e lo scheduler restano BSP: K3 è aperto.

## Prove finali

| Prova | Esito | Evidenza sotto `kernel/build/` |
|---|---|---|
| Supervisore C e coordinatore Rust, I/O modellato | 9/9 scenari | `k3i-sequence-host-release/tools.log` |
| Riarmo C/ASan, coordinatore e policy record/blob Rust reali, memoria modellata | 22/22 scenari | `k3i-sequence-host-release/rearm/results.json` |
| Monitor: risposta parziale e ripristino CPU selezionata su errore | 2/2 test | `k3i-sequence-host-release/registers.log` |
| Gruppo completo UEFI/OVMF | 10/10 consecutivi | `first-ap-firmware-395f31bd4ea342969b05cd91192b0d8a/results.json` |
| Errore di readback iniettato dopo scrittura fisica | 1/1 negativo previsto | `sequence-failure-35b8e7292a6c4755945d8e24ee89ab45/results.json` |
| Regressione primo AP | 10/10 consecutivi | `first-ap-firmware-636b446023964cc893651d3b23db5d24/results.json` |
| Regressione ordinaria, quattro vCPU configurate e solo BSP operativo | 10/10 consecutivi | `percpu-firmware-da0835a5e0944f1ab60f29ce99a401a3/results.json` |
| Regressione ordinaria, una vCPU | 10/10 consecutivi | `percpu-firmware-046b5ac0b6954bf3aa89221a1d8662d0/results.json` |

Il monitor verifica CR3, GS, stack, GDT/TR, IDT, WP/NX/SSE e IF di ogni AP,
record completi distinti, token 520/1040/1560, stato finale PARKED e
online=15. Controlla anche budget, pinning, guardie, codice e tabelle.
Il conteggio dei core non deriva dal solo parametro QEMU `smp=4`.
L'IDT definitiva è condivisa; GDT/TSS, IST, stack e contesti per-CPU sono
privati. L'IDT temporanea del prefisso alto è invece nel frame di ciascun AP.

I test host includono ultimo indice, indice alto 62→63, token/record errati,
provider e maschera incoerenti, sorgente futura già ammessa, codice alterato,
letture fallite, pin mancante, prestito rifiutato, scrittura parziale,
finestra assente, guardia persa, readback errato e controllo finale fallito.
Dopo il punto irreversibile verificano il rifiuto di un secondo tentativo.
I confini nativi della memoria, incluso il controllo del frame, sono mock
nell'host; il frame e il mapping reali sono esercitati nelle prove UEFI.

Il negativo guest altera la sola copia letta, non simula un guasto reale
della RAM. Conferma pagina fisica nuova completa, controller terminale,
token successivo PREPARED, nessuna ammissione del secondo AP e panic BSP
con ACK del primo AP. Codice, tabelle, budget e pin restano verificati.
Gli AP non ammessi possono essere ancora nel contesto di OVMF: non si
pretende che siano in real mode.

Compilazioni MSVC /W4 /WX e Rust -D warnings; ASan sul C host. Cppcheck
nelle suite e in `k3i-sequence-native-static/` senza diagnostiche bloccanti;
include mancanti e suggerimenti di stile sono conservati negli XML.

## Debug e ricontrollo

- Primo tentativo di riarmo: il prestito rifiutava il contesto nullo.
  Passato il controller come contesto e aggiunti stage d'errore distinti.
- Il nuovo fixture host usava inizialmente un alias di tipo inesistente;
  /WX ha bloccato la compilazione. Corretto senza abbassare i warning.
- Cppcheck ha segnalato I/O su file dentro assert: separate operazione e
  controllo del risultato. Nessuna soppressione aggiunta.
- La prima regressione del primo AP si è fermata a 4/10 per un dump HMP
  troncato prima di EFER. Il nuovo test riproduce il difetto; il reader
  aspetta il valore completo. Ripetuta da zero la campagna finale.
- I test Python scrivono su stdout per evitare NativeCommandError di
  PowerShell 5.1 sul normale output di unittest; exit nonzero resta errore.

Le prove preliminari, compresi i fallimenti, sono conservate e non sommate
alle campagne finali. Il prerequisito precedente delle tabelle live resta
verificato in `k3i-live-tables-host-final/` e `k3i-live-tables-joint-host/`:
Accessed ammesso nelle voci presenti, Dirty solo nelle foglie; la verifica
inattiva resta esatta e le voci vuote restano zero.

## Fonti, memoria e limiti

[Intel SDM 253668-078US](https://cdrdv2-public.intel.com/671190/253668-sdm-vol-3a.pdf):
§4.8 per A/D; §9.2.2 per ordine WB e §12.3 per UC, usati dal callback
con store/load scalari volatili prima delle IPI; §9.4.4.1 e §§11.5.3/11.6.1
per la sequenza INIT/SIPI/NMI già esistente. Nessun nuovo offset hardware
o codice Assembly aggiunto. Le generazioni e i record riusano Rust.

Nessuna nuova allocazione runtime; 8 KiB statici per preparare le pagine.
Il monitor misura accounting e pin guest, non un leak detector fisico.
ASan non strumenta Rust, Assembly o RAM guest; Valgrind, TSan e coverage
linee/branch N/D. Profilo fidato QEMU/TCG/OVMF, quattro vCPU; nessuna
certificazione hardware reale, hotplug o avvio parallelo.

Restano panic originato da AP e sul gruppo completo, integrazione ordinaria
e stress finale. Gli AP non eseguono ancora task dello scheduler.
Manifest immagini e sorgenti: `kernel/build/k3i-sequence-manifest.json`.
