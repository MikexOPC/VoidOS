# Cursore SVG e testo antialias — 16 ottobre 2026

## Contratto approvato

Sagoma fornita dall'utente in `scripts/pointer/left_ptr.svg`: riempimento
nero, contorno bianco centrato di 17 unità nel documento 256×256.
Conversione offline alla dimensione corrente 18×18, senza modificare
hotspot, coordinate, code mouse o protocollo save/restore. Curve campionate
e supersampling 16×; coperture nero/bianco quantizzate con somma massima 16.
Non è un parser SVG nel kernel e non supporta SVG arbitrari a runtime.

Scelta utente: antialiasing del testo in scala di grigi, mantenendo 9×18.
Il TTF Cascadia Mono già presente, con hash/licenza verificati, produce
109 glifi e righe di copertura a 4 bit per pixel. Raster offline 8× e filtro
BOX; nessun parser TTF o allocazione aggiunta al kernel. La tabella di
copertura occupa 15.696 byte, oltre ai bitmap mantenuti per compatibilità.
Gli altri font bitmap forniti dal chiamante restano monocromatici.

## Percorso e invarianti

1. Validare font, UTF-8, capacità e colori; pianificare al massimo 64 indici
   di glifo (256 byte), senza un grande array di pixel sullo stack.
2. Per il font incorporato leggere una riga di copertura; per un font
   bitmap tradurre i bit in copertura 0 oppure 15. Glifo assente: fallback.
3. Calcolare ogni canale come `(foreground*a + background*(15-a) + 7)/15`.
   Caret: copertura piena nelle ultime due righe della cella selezionata.
4. Usare gli stessi store e le stesse transazioni autorizzate; il cursore
   viene ripristinato prima del testo, salvato e composto dopo il testo.

RGB e BGR condividono il calcolo simmetrico dei canali. Nessun nuovo
registro, cache policy, accesso hardware o autorizzazione persistente.
La funzione interna `x86_fb_font_row` riceve il descrittore già verificato
dal renderer. Le tabelle incorporate sono immutabili; soltanto i loro
metadati originali selezionano la copertura antialias.

## Verifica

I test host controllano interpolazione, estremi, coordinate, font bitmap
precedenti, RGB/BGR, padding, caret, ripristino e sovrapposizione del
cursore. L'oracolo QEMU rasterizza il TTF e confronta pixel fisici e PPM;
condivide FreeType con il convertitore, quindi non è un secondo motore
tipografico indipendente. Rigenerazione `convert.py --check` separata.

Immagine `kernel/build/visual-aa/kernel.elf`, SHA256
`9F1B98A49A61100249C6C7FACAF9E5D89174903A4C98919E6CE28AA17E3562F3`.
Loader SHA256 `51AE55CA0B0E660F8D1CFA6B3114D588074047A12C448EB7ADEDA484826635AC`.

- Build MSVC/MASM/Rust con warning come errori: `visual-aa-build.log`.
- C/ASan, Rust debug/release/Clippy e cppcheck: `visual-aa-console.log`
  e `visual-aa-framebuffer-final.log`; sette gruppi lifecycle ×100,
  tre gruppi pixel e cinque gruppi drawing ×100. Interpolazione verificata
  separatamente su tutti i 16 livelli, con colori non uniformi.
- Dieci boot consecutivi: `visual-aa-qemu.log`, risultati in
  `ps2-input-firmware-84050a86f1fb450ab8e9e84140221204/results.json`.
  60 scene confrontate, 6000 invii continui; 651 eventi mouse per boot,
  zero perdite, budget 20.411 prima e dopo ciascun boot. Quattro pagine
  in più rispetto al precedente checkpoint, coerenti con la tabella font.
- Zero gruppi oltre 100 ms; massimo osservato 269.483 tick PM, circa
  75,3 ms. Non è una misura del framerate né un confronto controllato
  con il vecchio renderer.
- Preparazione launcher: `visual-aa-launcher.log`; ESP isolata e hash
  della stessa immagine. Screenshot QEMU: `visual-aa-qemu.png`.
- Scroll/riga piena: `visual-aa-stress.log`, risultati in
  `ps2-input-firmware-e0cdf0bc0d864af08113228b85909f7a/results.json`:
  612 tasti, riga troncata respinta, 46.358 celle, zero perdite/overrun,
  budget 20.411 ripristinato.
- Fallback senza video: `visual-aa-fallback.log`, risultati in
  `ps2-input-firmware-31c7671864ad45d480169d4232df48e4/results.json`:
  tastiera/shell/mouse e cleanup, zero perdite, budget 20.456 ripristinato.
- ESP del launcher avviata senza finestra: `visual-aa-launcher-smoke.log`;
  movimento, help, Esc, cleanup e KERNEL_READY verificati. Non certifica
  l'interazione umana con SDL.

I percorsi delle evidenze sopra sono relativi a `kernel/build/`.
ASan riguarda il C host; Valgrind non eseguito, coverage N/D. Nessuna
promessa di 60 FPS, testo vettoriale scalabile o Unicode completo.
