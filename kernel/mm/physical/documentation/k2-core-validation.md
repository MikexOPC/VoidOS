# K2-B: core interno e bootstrap BSP

Snapshot della preparazione del 28 ottobre 2026, precedente ai lock.
Per lo stato successivo vedere [k2-pool-validation.md](k2-pool-validation.md).

Preparazione verificata all'adozione dei lock;
**l'allocatore non è ancora sincronizzato**. Nessun SMP guest attestato.

## Implementazione

`allocator.c` conserva tutte le API pubbliche e delega al core in
`allocation/state.c`, dichiarato soltanto in `internal.h`. Il core
richiama kphys_validate, non il validatore pubblico che acquisirà il
lock. L'alias allocate delega ad allocate_aligned: il futuro ingresso
nel lock dovrà avvenire una sola volta. Holdings resta uno stato distinto.

Il confronto normalizzato con b3eb7e2 dimostra che il core deriva dal
vecchio allocator.c soltanto tramite estrazione, rinomina delle funzioni
interne e spostamento dell'alias. Header pubblico e metadati invariati:
nessun cambiamento ad allocazione, coalescenza, pinning o rollback.
Evidenza: `kernel/build/k2-physical-extraction.json`.

Aggiornati dodici adattatori che compilano l'allocatore, incluso quello
kernel. Conservata l'intercettazione del pinning nei test VT-d: soltanto
la facade viene rinominata; il core non elude gli errori iniettati.
Nessun include di file .c e nessun percorso di produzione senza controlli.

Il bootstrap ora inizializza una volta il contesto BSP prima di usare
l'allocatore, anche nel profilo diagnostico della memoria. Su rifiuto
si arresta prima di allocare. Il runtime sync è collegato anche al kernel
ordinario; il probe resta condizionato a TestKernelSync. Il probe verifica
il contesto già inizializzato e rifiuta la seconda init, anziché crearne
uno tardivamente. Il backend è quello già verificato: nessun nuovo
registro, Assembly o modello hardware. GS/per-CPU resta K3.

## Verifiche

- Suite kernel: PASS, `test-run-8fcfe6b023714ae89ba596942b4ca7c7/`.
- Nuova suite physical: PASS, `test-run-3993ab5b8ce741a9aa03338d1be7a66a/`.
  ASan C, dieci ripetizioni delle prove ownership/esaurimento/coalescenza/
  allineamento/rollback e accounting; due test Rust in debug e release,
  Clippy e cppcheck senza diagnostiche bloccanti.
  Dati: `physical-host-ad75bf27969543a9a8c1ebe426aefc91/`.
- Dieci suite degli utilizzatori: paging, dma-arena, virtio-resources,
  x86-virtio, pci-ecam, x86-framebuffer, iommu-domains, iommu-registers,
  iommu-activation, iommu-invalidation: tutte PASS. Riepilogo e percorsi
  effettivi in `kernel/build/k2-physical-validation.json`.
- Bootstrap host ASan: 369 controlli e 18 errori API iniettati, PASS;
  verifica l'ordine binding→allocatore e il fallimento prima delle claim.
  Mapping e allocatore sono mock, controllo CPU non eseguito sull'host.
  `test-run-91f2cc5321a74da88a8e915a6243dd34/`.
- Finale QEMU: 10/10 probe K2, 10/10 ordinari, inizializzazione BSP una
  sola volta; due fail-stop intenzionali passano senza KERNEL_READY.
  Probe: `sync-firmware-daaea37bce694cb0af48561e439010fa/`;
  ordinario: `sync-firmware-fa90cc7006e74e2ca2726bd260a43087/`;
  negativi nel riepilogo `qemu-run-6451836fdf474bf4b93ebcfbe777c097/`.
  Tutte le directory delle prove sono sotto kernel/build/.

La prima esecuzione della nuova suite ASan ha rilevato un errore nello
script: /TC applicato anche agli oggetti Rust. Corretto separando
compilazione C e link; la riesecuzione passa. Il fallimento precedente
è conservato, non contato come successo.

Nessuna allocazione dinamica aggiunta nel core o nel binding. ASan
strumenta C host e accounting dei modelli; non certifica leak di RAM
fisica del kernel. Valgrind non eseguito; coverage N/D. Nessuna contesa
multicore dell'allocatore ancora collaudata; il test multithread dei lock
non sostituisce questa prova.

## Lavoro ancora necessario

Inserire lock per owner nella facade, definire i ranghi dal grafo delle
chiamate, sincronizzare validazione/letture e migrare i lettori diretti di
Free/Live/Pins, budget e generazioni. Poi contesa del pool con contesti
host distinti e regressioni finali. Non avanzare all'heap dichiarando
il pool già concorrente. Il panic storico LAPIC resta non attribuito:
queste campagne dimostrano lo stato attuale, non la sua causa originale.

Nessun commit o push in questo blocco.
