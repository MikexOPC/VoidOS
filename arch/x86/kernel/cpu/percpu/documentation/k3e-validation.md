# K3-E — verifica dei contesti AP e del commit

29 ottobre 2026. Scelta approvata: rollback fino al tentativo di avvio AP.
Perimetro: cpu/percpu, relativi test e adattatori build/campagne.

## Comportamento implementato

Gli AP registrati ricevono contesti software distinti e PREPARED: Self e
indice verificati, EarlyContext nullo, token iniziale 1, nessun frame lock
attivo. Ciascuno possiede un descrittore di attesa e una copia numerica della
sorgente PM, senza riferimenti a temporanei o al contesto lock del BSP.
Il timer hardware resta comune; non e una calibrazione per core.

Le API di preparazione/rimozione sono interne al modulo e richiedono BSP,
IRQ disabilitati e assenza di lock detenuti. Nessuna ammissione AP nei lock:
l'accessor operativo corrente resta BSP-only. Gli oggetti sono statici e
non spostabili; nessuna allocazione heap aggiuntiva.

`x86_ap_resources_commit()` verifica registro e contesti, conferma il
mapping tramite inventario, rende non rilasciabili le lease e sigilla i
contesti. Dopo commit, unbind rifiuta prima di modificare root/pin/owner;
la rimozione dei contesti e delle loro attese e vietata. Una seconda
richiesta di commit e rifiutata. Se il sigillo fallisce dopo aver impegnato
le lease, arresto diagnostico con risorse conservate; nessun rollback
consentito dopo questo punto. Il futuro ingresso deve chiamare questa API
prima di qualsiasi tentativo INIT/SIPI.

Il boot ordinario prepara senza commit. Il profilo `-TestApCommit` prova
commit, rifiuto del secondo commit e del rilascio. `-TestKernelSync`
esercita anche cancellazione e nuova preparazione dei contesti prima
del commit. Nessun AP viene avviato, nessun GS/GDT/TSS AP viene caricato.

Rust mantiene le verifiche di identita e geometria K3-C/D; questo blocco
non introduce un nuovo validatore Rust o Assembly. Le nuove transizioni
di lifetime sono implementate in C, senza nuove istruzioni privilegiate.

## Host e analisi statica

- Clock/provider: 70 esecuzioni C ASan, delle quali 20 arresti attesi
  timeout/clock fermo. Preparazione fino a 63 AP, commit con conteggi 2,
  4 e 64, rifiuto con contesti extra, indici non validi, IF=1, CPU diversa,
  contesto alterato, doppia preparazione, fallimento del timer anche dopo
  un primo AP preparato, cancellazione e ripreparazione. BSP conservato.
  `kernel/build/percpu-clock-host-d2c8df15b8184d9ea1348a354df08687/`.
- Risorse: geometrie C/Rust, quattro esecuzioni dei due test Rust
  debug/release, sweep di tutti i bit dei 63 descrittori e 101 failure point
  del percorso di costruzione. Aggiunti commit rifiutato con verifica
  fallita e rollback ancora possibile; commit riuscito e unbind senza
  effetti su owner, root, budget, pin o chiamate al backend.
  `kernel/build/ap-resources-host-54a1777d0ff84a67bad3e6800ed83ec8/`.
- Provider GS: 50 esecuzioni positive/negative previste,
  `kernel/build/percpu-host-188e07c34ad3431798c2a3a82c7fca9a/`.
- Regressioni kernel, bootstrap e identita: tutte passate; log riepilogati
  in `kernel/build/k3e-kernel.log`, `k3e-bootstrap.log`, `k3e-identity.log`.
- MSVC /W4 /WX e link delle immagini finali riusciti. Cppcheck nei due
  harness e nel backend nativo: zero error/warning/performance/portability;
  restano suggerimenti style e informazioni, non soppressi globalmente.
  Le doppie chiamate opposte del diagnostico verificano intenzionalmente
  la transizione successo/rifiuto. Gli azzeramenti espliciti e la firma
  callback sono coerenti col runtime freestanding e con l'interfaccia.
- Manifest `kernel/build/k3e-source-review.json`: 41 sorgenti/script
  per-CPU, massimo 175 righe; nessuna cartella oltre cinque sorgenti,
  nessun trailing whitespace. Include hash dei file verificati.

## QEMU sulle immagini finali

Dieci boot consecutivi diagnostici con quattro vCPU configurate:
`kernel/build/percpu-firmware-5351fb11d18f4c76a7d8d876a5ec7025/`.
Immagine `kernel/build/k3e-final-diag/kernel.elf`, SHA-256
`CCD9BADBB4A655AEE104BDB03440882DD1EE41C90068ADBD1EC7FAD50895FDD6`.

Dieci boot consecutivi ordinari alternando una e quattro vCPU:
`kernel/build/percpu-firmware-1810b9910c0240c5b109f2ebde1e2604/`.
Immagine `kernel/build/k3e-final-ordinary/kernel.elf`, SHA-256
`CD92567B16B656175A3A9A7A7F01ABEB7B07839D3B84BC70549B8201E96149F5`.

Il monitor legge i contesti e gli slot timer realmente mappati: Self,
indice/stato, token, frame vuoti, puntatori privati, valori della sorgente
e legame con il rispettivo contesto. Conservate le verifiche GS, identita,
arena AP, PTE/guardie e lease pinned del precedente harness. VM possedute,
deadline, ESP e hash verificati. I precedenti boot esplorativi non
sostituiscono queste campagne finali.

Console VirtIO/VT-d: una sessione completa con quattro vCPU configurate,
pixel, movimento e cleanup verificati. Evidenza
`kernel/build/pointer-interactive-check-2f171d50d2de43efae2e648ae96c152c/`,
immagine `kernel/build/k3e-final-console/kernel.elf`.
L'inizializzazione PCI/IOMMU ha richiesto diversi minuti entro la deadline
esistente del test; nessun timeout o panic. Non e una misura prestazionale
del nuovo codice per-CPU. Il riepilogo verificato e in
`kernel/build/k3e-final-validation.json`.

Ripetizione diagnostica registrata nella campagna `percpu-ap-commit`:

    ./scripts/test/qemu.ps1 -Campaign percpu-ap-commit

## Limiti e prossimo gate

ASan riguarda il C host, non il kernel guest o Rust. Accounting e pin
sono verificati nei percorsi esercitati; le lease committed restano vive
intenzionalmente. Non equivale a zero leak globali. Valgrind non eseguito,
coverage linee/branch N/D, nessuna prova su hardware fisico.

Resta un solo core OS operativo. Prima dell'avvio AP servono trattamento
fault/NMI compatibile per CPU, riserva bassa prima degli allocator generici,
trampolino e CR3 di transizione, ingresso e protocollo di avvio verificati.
Non sono introdotti hotplug, shootdown o un protocollo di arresto AP.

K3-E verificato per il profilo BSP descritto; K3 complessivo resta aperto.
