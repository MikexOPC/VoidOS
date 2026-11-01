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
