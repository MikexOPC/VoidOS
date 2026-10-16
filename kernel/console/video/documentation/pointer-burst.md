# Input continuo e freccia arrotondata — 13 ottobre 2026

La seconda prova manuale ha invalidato l'aspettativa di fluidita del
checkpoint precedente. Sessione `pointer-interactive-3b8e35ea65774960ad3a14203dc6085d`:
557 eventi mouse, perdita della coda e `PS2_INPUT_FAILED`, con cursore
nascosto durante il cleanup. Non era un problema di trasparenza della bitmap.

Contratto approvato dall'utente: fino a32 pacchetti per turno, ordine e
pulsanti conservati, posizione piu recente disegnata dal worker, freccia
bianca arrotondata con bordo nero e antialiasing.

La task conserva il decoder incompleto quando deve attendere; cede la CPU
dopo32 eventi, errori o perdita. Nessuna modifica al limite256 byte della
coda e nessun successo dichiarato dopo una perdita. La console orienta
la scansione dirty alla cella della posizione piu recente, conservando
le celle precedenti da ripristinare. Nessun disegno nella task mouse o IRQ.

La freccia e un asset originale a curve, ispirato al riferimento utente.
Il generatore `scripts/pointer/shape.py` produce coperture4x4: nero e
bianco hanno pesi0..16 con somma massima16. Runtime intero senza float,
heap o librerie grafiche: tabella24x24 e composizione dei tre canali sul
colore della griglia RAM. Pixel RGB/BGR, trasparenza, caret, padding e
ripristino verificati dalla suite framebuffer. Il generatore richiede
Pillow soltanto sul computer host; non viene eseguito nel kernel.

Il test continuo passa da150 invii distanziati10ms a600 invii distanziati
5ms, senza aspettare ogni pacchetto. QEMU puo aggregare i delta: il test
confronta somma ricevuta, ordine dei pacchetti precedenti, posizione e pixel.
Non e una misura degli FPS o una prova di interazione umana con SDL.

## Prove finali

Immagine `kernel/build/pointer-burst-final/kernel.elf`, SHA256
`5E16856E6911F49CFD861C68C0E3C0394EBBBC33EF5ED1F409F09255042B58E4`.
Build con warning come errori: `kernel/build/pointer-burst-build-final.log`.

Dieci boot consecutivi: `kernel/build/pointer-burst-qemu.log` e
`kernel/build/ps2-input-firmware-43977f81bc194e55928c3cb4c1824470/results.json`.
Ogni boot verifica98 tasti,10 comandi,651 eventi mouse/1953 byte,
zero perdite,6 scene live con pixel fisici e screenshot, cleanup e
budget20417 ripristinato. Totale6000 invii continui e60 scene.
Massimo drain osservato dopo l'ultimo invio:47ms, non una misura degli FPS.
I boot2 e4 hanno un WARN per gruppo oltre100ms; non si promette latenza costante.

Stress testo/scroll:612 tasti,12 comandi, un comando troncato respinto,
651 eventi mouse e nessuna perdita/overrun. Evidenze:
`kernel/build/pointer-burst-stress.log` e
`kernel/build/ps2-input-firmware-b6a34d6b196649c3ac8c9efa7c0d02a8/results.json`.
Fallback senza video:35 eventi, zero perdite e budget20462 ripristinato;
`kernel/build/pointer-burst-no-video.log` e
`kernel/build/ps2-input-firmware-2e714e0b767d4646a0297d5a5d94d8d5/results.json`.

Host C/ASan, Rust debug/release/Clippy, Python e cppcheck:
`kernel/build/pointer-burst-mouse.log`, `pointer-burst-host-final.log`,
`pointer-burst-framebuffer.log`. I test mouse includono raffiche di85 pacchetti
con due yield, contro gli85 del precedente comportamento per pacchetto.
Il generatore rieseguito produce byte identici; screenshot reale del cursore:
`kernel/build/pointer-rounded-qemu.png`.
Review strutturale senza errori: `kernel/build/console-render-review.json`,
massimo398 righe sorgente; diff del perimetro senza whitespace errato.

Launcher in PowerShell5: `kernel/build/pointer-burst-launcher.log`.
Smoke dell'ESP preparata: `kernel/build/pointer-burst-launcher-smoke.log`;
mouse, help, Esc e cleanup passano con display none al posto di SDL.
Nessuna prova automatica di interazione umana nella finestra SDL.
Nessuna nuova allocazione dinamica; ASan host e accounting fisico QEMU
sono prove distinte. Valgrind non eseguito, coverage N/D, hardware reale
non verificato. Le scansioni complete del paging restano per gruppo:
il costo residuo richiede ancora misure e non e eliminato da questo fix.
