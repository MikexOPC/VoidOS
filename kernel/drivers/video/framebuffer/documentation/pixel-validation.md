# Primitive numeriche per pixel — 9 ottobre 2026

Snapshot precedente al blocco handoff v4. Stato corrente nel
[report handoff](handoff-validation.md); le prove sotto restano quelle
dell'immagine di questo blocco.

Nucleo Rust safe senza heap e gateway C con soli interi. Nuovo codice
con soli separatori; nessun codice o asset importato da reuse/.

## Contratti

fb_pixel_offset riceve geometria, formato, capacita del buffer e coordinate.
Ricalcola i byte della superficie entro i limiti v0; rifiuta buffer corto,
geometria invalida, x>=width e y>=height, inclusi i pixel del padding.
Offset in byte dal principio del buffer; UINT64_MAX segnala errore.
Moltiplicazioni/addizioni checked. Capacita e coordinate sono numeri:
la funzione non dimostra mapping, ownership, lifetime o policy cache.
Un caller futuro deve passare la capacita realmente accessibile, verificare
il risultato e possedere il mapping prima dello store.

fb_color_word produce i quattro byte little-endian R,G,B,0 o B,G,R,0
per i formati GOP0/1. UINT32_MAX per formato invalido; il byte riservato
sempre zero rende distinta la sentinella anche dal bianco.
Fonte: UEFI2.10 §12.9.2, collegata nel [progetto](design.md).
Nessun nuovo registro, accesso MMIO o disegno nella memoria video.

## Prove host

[Log tool](../../../../../kernel/build/framebuffer-pixel-final/tools.log):
8 casi Rust distinti, debug e release con overflow checks e warning
come errori. La suite mode ha4 casi; la suite pixel ha4 nuovi casi e
riesegue i4 mode:12 esecuzioni per profilo, non12 casi distinti.
Clippy su nuclei e gateway. Superfici1..16 per lato, padding0..4,
coordinate interne/esterne, buffer corti, valori massimi e canali0..255.

C MSVC /W4 /WX e ASan:190.193 controlli,10.000 sessioni e30.000
allocazioni host bilanciate; sette nuovi controlli verificano anche
il confine ABI C/Rust. Nessuna allocazione nelle nuove primitive.
[Cppcheck](../../../../../kernel/build/framebuffer-pixel-final/cppcheck.xml):
zero error/warning/performance/portability, con i limiti gia descritti
nel [report GOP](validation.md). Rust non e strumentato da ASan;
Valgrind, coverage e leak globali del firmware restano N/D.

Clippy ha rilevato la doppia inclusione del nucleo mode nel gateway:
corretta tramite re-export della singola istanza, poi suite completa
rieseguita con successo. Il log conserva anche il tentativo fallito.

## Prove firmware

[Dieci boot consecutivi](../../../../../kernel/build/framebuffer-firmware-dd5fa09675694c5abd6f9a622d285f43/results.json),
immagine SHA256:
5A15107B41332AAF00E830A840759C85160B02FC497885B38F4B4EF04387C739.
Probe EFI q35/TCG/max/BSP: selezione GOP e controlli numeric-first/last,
x fuori schermo, buffer corto e ordine colore, marker GOP_PIXEL_VALIDATED
prima di GOP_SELECTION_READY. Screenshot conferma solo la geometria del
modo firmware: non dimostra rendering del kernel. Nessun triple fault;
loader ordinario invariato secondo hash prima/dopo.

[Caso senza video](../../../../../kernel/build/framebuffer-firmware-f08080329b4848af81634becdf216720/results.json):
1/1 sulla stessa immagine, marker GOP_UNAVAILABLE e nessuna adozione
di un framebuffer assente.

Il contratto di boot ordinario rimane v3. Handoff v4 opzionale e mapping
video dedicato sono il prossimo blocco; queste primitive non li sostituiscono.
