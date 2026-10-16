# Transazione cursore — verifica del 14 ottobre 2026

Contratto approvato: [save/restore](cursor-transaction-design.md).
Un solo worker possiede il backup statico di576 word e coordina
ripristino, testo/caret, salvataggio e composizione. Nessuna nuova allocazione.

`drawing/cursor.c` associa il backup all'owner originale, root e superficie
completa. Un errore dopo l'apertura della transazione resta terminale;
nessuna nuova chiamata viene accettata dal backend dopo quel fallimento.
L'aggiornamento dei metadati del backup avviene soltanto dopo la verifica
finale. Stop ripristina il vecchio sfondo senza disegnare la freccia.

`pixels/transaction.c` valida fino a3 rettangoli separati, somma area2176,
dimensioni, stride, Used e limite dei byte realmente mappati prima degli
accessi. Nessuna autorizzazione della bounding box fra rettangoli distanti.
Una verifica completa dell'inventario apre la transazione; i punti usano
sessione originale, root, stato mapping e rettangoli catturati. Il controllo
di autorita finale resta quello del backend precedente. Le autorizzazioni
non sopravvivono alla fine della transazione.

Il limite2752 conta operazioni API, ogni store aggiunge un readback;
il traffico MMIO teorico massimo e4928 accessi. Pixel raw letti/salvati
mantengono il byte riservato per il ripristino. Colori nuovi rimangono
a24bit e la composizione antialias ignora il byte riservato.
Fonte: [UEFI2.10 §12.9 GOP](https://uefi.org/specs/UEFI/2.10/12_Protocols_Console_Support.html),
pixel RGB/BGR reserved8bit, offset `(y*PixelsPerScanLine+x)*4` nel mapping
kernel gia verificato. Nessun nuovo registro o modifica della cache UC.

`kernel_pointer_request` aggiorna coordinate/pulsanti senza marcare il
percorso intermedio. Il worker attiva una cella alla posizione finale;
il resto del ripristino viene dal backup, non dal ridisegno dei vecchi glifi.
Le vecchie API pointer e diagnostiche rimangono disponibili; il nuovo
percorso e limitato ai gruppi di8 celle e al font16pixel della console.

## Host

C/ASan e cppcheck: `kernel/build/cursor-transaction-host-final.log`.
Test: RGB/BGR, trasparenza, caret, padding, restore/hide, rettangoli distanti,
rifiuto del gap fra rettangoli, transaction copiata, chiusura doppia,
2752 operazioni ammesse e2753 respinta, geometria invalida.
Verifica ownership finale: `kernel/build/cursor-transaction-ownership-host.log`.
Durante la transazione il mapping assume `X86_FB_RENDERING`: la revoca
esistente ammette soltanto stati1..3 e quindi lo rifiuta senza liberare
tabelle o modificare la root. Anche una seconda apertura e rifiutata.
End valido ripristina lo stato2. Abort richiede zero scritture tentate
e autorita ancora valida; dopo scritture o autorita persa il mapping resta
occupato. Questi casi sono coperti dalla suite C/ASan.
Console C/ASan, Rust debug/release/Clippy e Python:
`kernel/build/cursor-transaction-console-verified.log`; request senza dirty,
interfacce task3/worker, ownership, failure, accounting e cleanup.
La regressione del pacchetto senza spostamento verifica zero disegni e
zero nuovi segnali del servizio console; i pulsanti restano registrati.
Queste suite usano mock di firmware/paging/IRQ; i pixel e le funzioni C
testate sono reali. Rust non e strumentato da ASan.

## QEMU

Primo smoke intermedio: `kernel/build/cursor-transaction-smoke.log`.
Non sostituisce la campagna della modifica finale.
Immagine finale `kernel/build/cursor-transaction-final/kernel.elf`, SHA256
`C7B4C4896421D1D3905E97A300135BFEC54AE46402165E58311B3BA287F19100`.
Build /WX: `kernel/build/cursor-transaction-ownership-build.log`.
La prima sequenza di dieci boot, `cursor-transaction-qemu.log`, precede
la correzione dei pacchetti senza spostamento; resta un checkpoint.
Anche `cursor-transaction-qemu-final.log` precede la protezione esplicita
della revoca e resta un checkpoint. Campagna definitiva completata:
`kernel/build/cursor-transaction-ownership-qemu.log` e
`kernel/build/ps2-input-firmware-c2ff52ad031d493999a3c1171e58c19c/results.json`.
Dieci boot consecutivi,98 tasti/10 comandi e651 eventi mouse per boot,
6000 invii continui,60 scene confrontate con screenshot e pixel fisici.
Zero perdite, zero panic e zero gruppi oltre100ms. Massimo gruppo
osservato37,02ms; massimo drain dopo l'ultimo invio46ms: non sono FPS.
Budget20415 prima/dopo in tutti i boot; la differenza rispetto ai20417
del vecchio binario riguarda le pagine dell'immagine piu grande, non
allocazioni runtime perse. Nessun confronto controllato delle prestazioni.

Stress:612 tasti,12 comandi, scroll e651 eventi mouse; zero perdite,
zero overruns, budget20415 ripristinato. Evidenze:
`kernel/build/cursor-transaction-ownership-stress.log` e
`kernel/build/ps2-input-firmware-80978560d7414395a35d891f4517c7dd/results.json`.
Fallback senza video:98 tasti/10 comandi,35 eventi, zero perdite,
budget20460 ripristinato; `kernel/build/cursor-transaction-ownership-no-video.log`
e `kernel/build/ps2-input-firmware-fe36d2b03c3141ba8babf94f944cdf58/results.json`.

Review: `kernel/build/console-render-review.json`, zero errori,
massimo398 righe sorgente. Build script400 righe; pixels/ contiene5
file sorgenti diretti. Diff del perimetro senza trailing whitespace.
Launcher PowerShell5: `kernel/build/cursor-transaction-launcher.log`.
Smoke dell'ESP preparata: `kernel/build/cursor-transaction-launcher-smoke.log`;
mouse, help, Esc e cleanup passano con display none al posto di SDL.

## Limiti

Nessuna atomicita di scansione video/vblank garantita. Double buffering e
driver GPU non implementati. La transazione riduce il ridisegno del cursore,
ma mantiene le scansioni di paging: nessuna promessa di60FPS. Non e un
benchmark controllato prima/dopo. Valgrind non eseguito, coverage N/D;
ASan host e budget fisico QEMU sono evidenze distinte. Nessun hardware
fisico o uso umano della finestra SDL certificato da questa suite.
