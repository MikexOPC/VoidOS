# K4-A — Evidenze e lavoro rimanente

2 novembre 2026. Primo incremento verificato: protocollo RAM Rust,
interfaccia C e attesa sorvegliata C. Non ancora linkato nel kernel;
gli AP del boot ordinario mantengono il parcheggio K3 a IF=0.
K4-A nel suo complesso non è pronta: nessuna prova di IPI hardware nuova.

## Prove eseguite

Comando dalla root: `powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/test/run.ps1 -Suite ipi-service`.

- Rust: 6/6 test, con warning negati. Configurazione e limiti fino a 64 CPU,
  ownership logica, lifecycle, token stantii, doppio consumo, esaurimento
  generazioni, errore terminale, contesa con tre consumer e gara ACK/timeout.
- C/FFI: 20/20 processi, ciascuno con 15.000 richieste e tre consumer
  concorrenti: 300.000 richieste sul vero oggetto Rust ottimizzato.
  Tutti gli handle Windows posseduti vengono chiusi.
- Attesa C: 9/9 processi, ACK, errore clock, clock fermo, salto indietro,
  valore fuori maschera, wrap 24/32 bit, deadline e argomenti invalidi.
- MSVC C11 `/W4 /WX`, ASan sui sorgenti C, Rust `-D warnings`;
  cppcheck sui tre file C senza diagnostiche bloccanti.
- Suite `layout`: PASS. Limiti correnti 1.000 righe e 8 file diretti;
  sorgente più lungo del blocco: test Rust, 160 righe.

Log finale della suite:
`kernel/build/test-run-3b036bdd6a78496e9c8d379dd0af7da7/ipi-service.log`.
Oggetti, eseguibili e report cppcheck:
`kernel/build/ipi-service-host-98b478a4ed714cbca74ab084010bdbc7/`.
Layout: `kernel/build/test-run-d01c82ddcbe54aa594b8c5929235e760/layout.log`.

## Limiti delle prove

ASan strumenta C, non l'oggetto Rust. Le atomiche Rust sono reali, ma i thread
sono host Windows, non AP QEMU. Il clock dei test C è modellato: nessuna
lettura nuova ACPI/MMIO. Il modulo di produzione non alloca memoria dinamica;
ASan pulito e handle chiusi non sono una misura dei leak di RAM fisica.
Valgrind non disponibile nel PATH. Coverage linee/branch: N/D.

QEMU non eseguito per questo incremento non collegato all'immagine guest.
Le campagne K3 precedenti non vengono attribuite al nuovo protocollo.
Non sono dimostrati identità CPU reale, ordering MMIO, EOI, conservazione
del contesto interrotto, ritorno IRETQ o consegna dell'IPI.

## Prossimo incremento nello stesso modulo

Profilo diagnostico separato già approvato: predisposizione IDT/LAPIC,
ingresso Assembly con contesto preservato, mailbox dal contesto GS verificato
e PING hardware. Poi 10 boot consecutivi su quattro CPU realmente avviate,
prove negative e regressioni K3 come da [design](k4a-design.md).
Solo dopo queste prove si potrà integrare il servizio nel boot ordinario.
