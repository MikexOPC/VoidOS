# Framebuffer del vecchio OS: valutazione per riuso

Analisi statica del 9 ottobre 2026. `reuse/` resta intatto; nessun codice
o asset e stato importato. Non sono stati compilati o eseguiti i componenti
vecchi: questa valutazione non certifica il loro funzionamento.

## Parti da recuperare con adattamento

- `writer/writer_console.rs`: blending intero, conversione RGB/BGR,
  fallback del glifo e avanzamento del cursore. Sono primitive semplici
  da isolare e verificare senza compositor o heap.
- `draw.rs`: rasterizzazione per riga, evitando una chiamata FFI per pixel.
  Il vantaggio prestazionale va misurato sul nuovo backend.
- `gui/primitives/rect.rs` e Layer: clipping e rettangoli modificati
  separati dalla presentazione. Utili dopo una prima console funzionante.
- Stato terminale con colore per carattere: utile per conservare i tag
  dei log nello scrollback; non serve importare finestre e desktop.

## Correzioni necessarie prima di importare

1. `reuse/LunaroidOS-main/kernel/src/gui/ffi.rs:222`: la funzione Rust
   sicura `blit_glyph_row` passa al C puntatore e geometria, senza verificare
   la capacita della slice. Il C (`gui/cblit/blit.c:272`) controlla le
   coordinate contro width/height, non la lunghezza reale. Una slice vuota,
   geometria 1x1 e una riga con intensita255 portano a uno store fuori buffer.
   Richiedere capacita, stride e offset verificati prima della FFI.
2. `gui/ffi.rs:101,124`: calcoli di capacita con moltiplicazione/addizione
   non checked possono traboccare in release e aggirare gli assert.
   Usare checked_mul/checked_add e restituire errore prima della FFI.
3. `gui/primitives/layer.rs:12,25`: dimensioni e Vec pubblici possono
   perdere coerenza; allocazione width*height*4 senza overflow check.
   Rendere privati gli invarianti, con costruzione/ridimensionamento fallibili.
4. `writer/writer_console.rs:182`: limite sul buffer senza controllo
   separato x<width/y<height; x oltre larghezza puo scrivere padding o
   la riga successiva. Inoltre offset/bpp richiedono validazione esplicita.
   Lo scroll (:38) assume buffer alto almeno una riga di testo.
5. `api/api_lifecycle.rs:45` e `writer/writer_new.rs:49`: il passaggio
   a virtio usa width*height*4 e sostituisce solo il buffer, conservando
   i metadati del compositor. Con GOP stride>width, il nuovo buffer packed
   e i vecchi metadati divergono. Trasferire backend e descrittore insieme.
6. `types.rs:64`: history.remove(0) sposta tutte le righe; 500 righe
   non sono un budget in byte. Eventuale scrollback con ring buffer e
   limiti di caratteri/memoria definiti, senza allocazioni illimitate.

Questi sono problemi del contratto sorgente; non dimostrano un exploit
da userspace o un crash osservato nel vecchio profilo di boot.

## Cosa rinviare

Window manager, dock, Slint, compositor completo e virtio-gpu hanno
dipendenze e ownership propri: non sono necessari per la console v0.
Anche force_unlock nel panic richiede una politica distinta: rimuovere
un lock non ripristina automaticamente gli invarianti interrotti.

Il vecchio raster usa noto-sans-mono-bitmap 0.3, non semplicemente i TTF
in assets/fonts. Provenienza e licenze di libreria/font/asset vanno
verificate prima dell'importazione; i nomi dei file non sono una prova.

## Ordine proposto

Completare handoff e mapping video verificato; poi recuperare blending,
pixel e glifi in primitive con buffer/stride validati. Test host su buffer
corti, padding, RGB/BGR, clipping, overflow e schermi piccoli; quindi
pattern e testo verificati in QEMU. Dirty rectangles e scrollback solo
dopo la console di base. Nessuna promessa di accelerazione senza misure.
