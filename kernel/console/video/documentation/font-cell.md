# Cella console 9x18 (da 10x20) — 16 ottobre 2026

Aggiornamento richiesto dall'utente: font un poco piu piccolo. La cella e ora 9x18 (113 colonne
x 42 righe a 1024x768, area usata 1017x756). Generatore: corpo 15,36 px (advance 9 px), supercampionato 8x senza hinting e mediato (box), baseline
sulla riga 14, soglia 90, 109 glifi con righe da 9 bit. Costanti aggiornate: `X86_FB_CELL_*`,
`KERNEL_CONSOLE_CELL_*`, `font_valid` accetta Width 8 o 9, altezza 18 valida in Rust. Budget del
cursore: area 324 + 1296 + 324 = 1944, accessi 3 x 324 + 1296 = 2268. Una chiamata di testo
contiene al massimo 25 celle. Le larghezze dispari richiedono di leggere 5 parole da 8 byte per
cella negli script (`(WIDTH + 1) // 2`). Le sezioni seguenti descrivono il passaggio 8x16 -> 10x20:
dove citano 10x20, 20 celle, 2248/2572 o 102x38 valgono i valori sopra.

Puntatore: il riempimento nero dell'immagine di riferimento era invisibile sulla console scura
(rimaneva solo il contorno bianco); ora e grigio nitido (campione 6 << 5). Forma presa dall'SVG dell'utente (scripts/pointer/left_ptr.svg, rasterizzato da svg_art.py), hotspot (1,0).
Prove: 12 suite host, QEMU 10 boot per mouse+puntatore+shell (60 scene pixel per pixel),
stress da 200 tasti, 3 boot senza video, diagnostici framebuffer/console x10, ordinari x10
(`kernel/build/font9-campaign`).


Scelte utente (16 ottobre): font della shell piu grande, cella 10x20 invece di 8x16
(a 1024x768: 102 colonne x 38 righe invece di 128 x 48), insieme al puntatore 18x18
([pointer-sharp.md](pointer-sharp.md)). Fonte invariata: Cascadia Mono v2407.24 (OFL), vedi
[font.md](../../../../arch/x86/kernel/drivers/video/framebuffer/documentation/font.md).

## Font

Generatore `arch/x86/kernel/drivers/video/framebuffer/scripts/font/convert.py`:
corpo 17 pixel (l'advance di Cascadia, 0,586 em, diventa esattamente 10 pixel),
baseline sulla riga 16 (ascent 16, discesa 4 = 20 righe), layout BASIC. Il raster viene
disegnato su una tela piu larga della cella: l'inchiostro fuori da 10x20 e un errore,
non viene tagliato. Soglia 100 invece di 128: nel confronto visivo con 128 e 80
(`kernel/build/font-preview.png`) 100 da tratti piu pieni senza impastare; e una scelta
estetica, non una misura. 109 glifi, righe da 10 bit (bit 9 a sinistra).

## Contratto del codice

- `X86_FB_GLYPH.Rows` e `X86_FB_FONT.Missing` sono `unsigned short[20]`; `X86_FB_FONT`
  ha il campo `Width` (8 o 10). I font di test 8x8 restano validi con `Width = 8`.
- Percorso del cursore e del caret: richiede esattamente 10x20 (`X86_FB_CELL_WIDTH/HEIGHT`);
  il caret e una sottolineatura di 2 righe (18 e 19) a tutta larghezza.
- Il gateway `x86_fb_cell/group` ricava colonne e righe dalla dimensione del font.
- Un'unica chiamata di testo contiene al massimo 20 celle (4000 pixel, limite 4096);
  gruppi del worker: 8 celle = 1600 pixel.
- La console grid resta dimensionata per 240x67 (tetto, non piu la dimensione reale);
  `KERNEL_CONSOLE_CELL_WIDTH/HEIGHT` in `console.h` danno la cella al puntatore e
  `console/runtime.c` verifica con `_Static_assert` che coincidano con quelle del font.
  Il clamp del puntatore accetta fino a 2400x1340 pixel (240x67 celle).
- Budget della transazione del cursore: area 324 + 1600 + 324 = 2248; accessi
  3 x 324 + 1600 = 2572.
- A 1024x768 la griglia usa 1020x760 pixel: 4 pixel a destra e 8 in basso restano fuori.

## Test e harness aggiornati

Test C/Rust: font di console, caret, gateway (colonne 0..191, righe 0..53 a 1920x1080),
transazioni, movimento del puntatore. Nel test del font ogni riga di ogni glifo e controllata
entro 10 bit. Harness Python: `shell/pixels.py`, `cell.py`, `active.py`, `drawing.py` e
`pointer/firmware.py` leggono WIDTH/HEIGHT dal generatore; la sonda di disegno scrive la
tabella ASCII in due chiamate da 16 caratteri per riga.

## Prove

Suite host (C/ASan, Rust, test Python, cppcheck): vedere il riepilogo del commit.
Campagne QEMU sulle immagini di questa modifica (tutte con exit 0):

- Mouse + puntatore + testo + shell + console, 10 boot consecutivi, immagine SHA256
  `426FFC85EAEF1900C6EB295FB32908CB258FD9F2C398D71702A8F94888ED696B`, budget 20415
  ripristinato in tutti, 60 scene confrontate pixel per pixel con 324 pixel fisici del
  puntatore ciascuna, `kernel/build/ps2-input-firmware-3e3d1aeac8934848a27770df9d998a39`;
  stress da 200 tasti con righe lunghe (`...d3f71be5567246d1b059c863016f1cd7`);
  3 boot senza video (`...f0b1b2fb5c454a84949535300387dd06`).
- Diagnostico framebuffer (disegno, tabella ASCII, console render), 10 boot, 39445 pixel
  fisici per boot (`kernel/build/framebuffer-live-1db2f9f3ecd0483d96387b27b4b92d36`) e
  10 boot senza video (`...910fad05f83c4642af7e2e43bf06b4eb`).
- Console diagnostica `-TestConsoleDraw` e `-TestConsoleBatch`, 10 boot ciascuno
  (`...b8b2f77d03e147ed8c3d351971b50669`, `...02ddaddd96404fb6b005b5063dce5bc4`).
- Profilo ordinario, 10 boot (`kernel/build/scheduler-firmware-87de35fe092d42b698758d5dadd94284`).
- Un screenshot QEMU reale della shell conferma il testo a 10x20.

## Prestazioni

Nei 10 boot con mouse il flush con lavoro costa in media 2,6–3,0 ms (prima della modifica
2,4–2,9 ms) e il totale per boot e 3,6–4,6 s (prima 4,2–4,9 s, con piu flush). Il massimo
per flush e 9,6–54 ms (prima 9–14 ms, un boot a 23 ms): quattro boot su dieci superano
30 ms, possibile effetto di un gruppo da 1600 pixel invece di 1024 oppure del rumore
dell'host; non misurato oltre. La fluidita non e certificata ([flush-cost.md](flush-cost.md)).

## Limiti

Solo ASCII, accenti italiani maiuscoli e minuscoli, euro e fallback (109 scalari). Hardware
fisico e risoluzioni diverse da 1024x768 non provate in QEMU; la geometria e verificata dai
test host fino a 1920x1080. Le celle non sono ancora un parametro di profilo: cambiare
dimensione richiede di aggiornare le costanti elencate sopra.
