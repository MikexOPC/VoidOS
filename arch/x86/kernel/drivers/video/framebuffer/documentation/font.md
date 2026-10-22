# Aggiornamento corrente — 16 ottobre 2026

Il font corrente è Cascadia Mono 9×18 con antialiasing a16 livelli,
convertito offline. Design e prove nel
[report della qualità visiva](../../../../../../../kernel/console/video/documentation/visual-quality.md).
Le sezioni seguenti descrivono i checkpoint precedenti del font bitmap.

# Font console 9x18 (storico 8x16 e 10x20) — aggiornato il 16 ottobre 2026

Dal 16 ottobre la cella e 9x18 (prima 10x20): parametri del generatore, contratto e prove in
[font-cell.md](../../../../../../kernel/console/video/documentation/font-cell.md). Le sezioni
sotto descrivono il checkpoint del 10 ottobre con la cella 8x16 e restano come storico;
dove citano 8x16, soglia 128, 16 righe o 32 celle valgono i nuovi valori del documento
richiamato.

Scelta utente: Cascadia Mono per la console; Barlow resta riferimento
futuro per la GUI. Modulo framebuffer x86, nessun cambiamento alla shell.

Fonte ufficiale [Cascadia v2407.24](https://github.com/microsoft/cascadia-code/releases/tag/v2407.24):
ttf/static/CascadiaMono-Regular.ttf, preservato in font/source/ con hash
in origin.json e LICENSE originale. Nessuna installazione sul computer.
Il bitmap derivato si chiama MikeSW Console. Dati e sorgente font restano
SIL OFL1.1: copyright/licenza conservati, nome riservato non usato come
nome del derivato. La licenza e specifica del font; non cambia quella OS.
[Licenza upstream](https://github.com/microsoft/cascadia-code/blob/v2407.24/LICENSE).

## Conversione e contratto

Conversione offline riproducibile con Pillow12.3.0/FreeType2.14.3, font
13 pixel, baseline12, soglia128 e layout BASIC. Bounding box verificati
prima del raster: nessun glifo fuori dalla cella8x16, advance8. Tutti i
glifi non spazio visibili e diversi dal fallback. Fonte/hash controllati;
--check confronta l'intero C generato. Nessun parser TTF, FreeType o heap
aggiunto al kernel: compila la tabella font/font.c gia generata.
[API Pillow](https://pillow.readthedocs.io/en/stable/reference/ImageFont.html).

109 scalar: ASCII32..126, ÀÈÉÌÒÙ/àèéìòù, euro e U+FFFD. Non e Unicode
completo. Bit7 a sinistra,16 righe per glifo; dati bitmap sotto OFL.
X86_FB_FONT ora dichiara Height8 oppure16, validata in Rust. I vecchi
bitmap8x8 continuano a funzionare con Height8 esplicita. Metadati non
serializzati: aggiornamento interno, nessuna modifica del protocollo boot.

Resta una sola verifica completa del mapping per chiamata, IF=0/BSP,
input in RAM fidata stabile, font ordinati/capacita esplicita e readback
UC32 bit. Stesse [fonti hardware](drawing.md). Preparazione locale1024
byte, zero nuove allocazioni. Limite4096 pixel invariato:32 celle8x16
oppure64 celle8x8. Righe maggiori respinte prima degli store; un servizio
console futuro dovra suddividerle. Nessuna promessa di latenza IRQ.

## Prove e limiti

Rigenerazione completa --check passata. Sedici casi Rust debug/release e
Clippy, sette gruppi lifecycle x100, tre gruppi pixel, quattro gruppi
drawing x100 con C/ASan/accounting. Nuovi casi: tutti i4096 pixel di32
celle16 righe,33 celle respinte senza scrivere, altezze0/17/MAX respinte;
regressione8x8 completa. Cppcheck14 TU senza diagnostiche bloccanti.
ASan C host soltanto; Rust e RAM fisica non strumentati. Valgrind non
disponibile, coverage N/D. Log kernel/build/framebuffer-font-host.log.

Diagnostico QEMU mantiene il raster precedente e aggiunge una frase con
accenti/euro e tutti i95 caratteri ASCII stampabili, in operazioni bounded.
L'oracolo host rasterizza direttamente il TTF e confronta il framebuffer
fisico e il PPM; --check confronta separatamente il dato C generato.
E una prova di conversione/instradamento/store, non un benchmark o un
test di tipografia indipendente da FreeType. Disegni solo diagnostici.
Hardware fisico, GUI e console shell operativa restano fuori.

Riproduzione: python scripts/font/convert.py --check dalla cartella
del modulo, poi scripts/test.ps1 e build kernel con TestFramebufferMapping.
Provider scripts/firmware.py: --rollback --pixels --drawing --boots 10.
Usare Python con Pillow/FreeType indicati.

## Campagne finali

Trenta boot finali, tutti KERNEL_READY, nessun fault inatteso, loader
invariato. Diagnostico10/10 con16837 pixel fisici/visibili per boot,
768 foglie video e guardie verificate. Ordinario10/10 senza probe video,
senza display10/10 sulla stessa immagine ordinaria. Risultati sotto kernel/build/:

- framebuffer-live-abb6b9c5c55b43f38869d472e008db8e/results.json (diagnostico).
- framebuffer-live-d51755e6ca0d472fa1d015c8266200e7/results.json (ordinario).
- framebuffer-live-c03c81bc1d6e4bd5a65c4fd3abd88774/results.json (assenza video).

SHA256 diagnostico F530ED454D3E178FAE246281781ADB95F1FE3BF115DA8D032B47D1410ECECB95.
SHA256 ordinario 3BD919218D4AF26AA329E27FC786A767F27715B4773F7FCBA4C6F17985FF6A0C.
Review finale kernel/build/framebuffer-font-review.json: righe/cartelle,
link, whitespace e diff conformi. ESP aggiornata con ordinario verificato
e licenza font separata MikeSW-Console-OFL.txt, hash copia confrontato;
backup precedente in kernel/build/framebuffer-font-previous.elf.
