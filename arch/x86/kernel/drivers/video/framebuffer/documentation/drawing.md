# Rettangoli e testo — contratto v0, 10 ottobre 2026

Estensione del writer [pixel](pixels.md), nello stesso modulo framebuffer
x86. Scelta utente: UTF-8 con lettere italiane, glifo visibile per scalar
assenti, font bitmap fornito dal chiamante, massimo4096 pixel per operazione,
coordinate fuori schermo respinte senza clipping.

## Progetto e fonti

Il chiamante kernel mantiene owner, testo e font in RAM valida, leggibile,
stabile per tutta la chiamata. Non e un'interfaccia userspace: la capacita
dichiarata non dimostra da sola la validita di un puntatore arbitrario.
BSP con IF=0, nessun AP o writer concorrente; non introdurre accesso da
task con IF=1 senza un contratto distinto. Nessuna allocazione heap/pagine.

Flusso concettuale, non codice eseguibile:

1. Copiare metadati e preparare bitmap/colori; respingere errori di input.
2. Rust controlla geometria visibile e budget4096 pixel; verificare primo
   e ultimo offset rispetto a Used, pagine mappate e overflow virtuale.
3. Verificare una volta l'intero inventario e le autorita/lease CPU.
4. Store UC scalari32 bit con readback; infine ricontrollare l'autorita CPU.

L'ambiente IF=0/BSP impedisce mutazioni software durante la chiamata.
Non protegge da DMA o da scritture concorrenti su altri core. La sessione
in pixels/session.h e interna e sullo stack: non conserva una vista
pubblica tra operazioni. Nessuna chiamata esterna durante gli store,
tranne il backend di indirizzo interno. Il mapping non viene cambiato.

[UEFI2.10 §12.9.2](https://uefi.org/specs/UEFI/2.10/12_Protocols_Console_Support.html):
RGB/BGR32 bit, Base al pixel0,0 e righe con PixelsPerScanLine. Resta lo
store/readback GPR UC del writer, con byte riservato zero.
[Intel SDM Vol.3A §14.3 tabella14-2](https://cdrdv2-public.intel.com/874249/253668-090-sdm-vol-3a.pdf):
policy UC forte gia verificata, nessuna modifica PAT/MTRR.

[Unicode17.0 §3.9.3 D92, tabella3-7](https://www.unicode.org/versions/Unicode17.0.0/core-spec/chapter-3/):
UTF-8 da1 a4 byte, esclusi overlong, surrogate e valori oltre U+10FFFF.
Rust valida direttamente i range della tabella su al massimo quattro byte
passati per valore, senza unsafe, puntatori ABI o runtime core esterno.
Sequenze malformate e controlli C0/C1 sono
errori prima degli store; uno scalar valido assente dal font usa Missing.
Questa politica distingue testo malformato da glifo non disponibile.

## Interfacce e invarianti

drawing/draw.h: x86_fb_rectangle con colore0x00RRGGBB; x86_fb_text con
metadati X86_FB_TEXT e X86_FB_FONT. Rettangoli non vuoti, nessun clipping;
area massima4096. Geometria e formato provengono dal mapping live validato.
Lavoro pixel O(area), un solo scan delle PTE indipendente dall'area.
Il limite controlla il lavoro; non e una misura di latenza IRQ garantita.

Testo non vuoto, massimo256 byte e64 scalar, celle8x8 opache, bit7 a
sinistra. Una sola riga, avanzamento8 pixel; nessun newline, tab, wrapping,
shaping, normalizzazione o sequenza ANSI. Accenti precomposti renderizzati
se il chiamante ne fornisce il bitmap; segni combinanti sono celle distinte.
Font fino a128 glifi ordinati per scalar unico, capacita esplicita in byte,
bitmap Missing obbligatoriamente visibile. Il driver non impone un font
globale: il probe usa dieci glifi originali propri, non un font completo
della console. Nessun codice/font copiato da reuse/ o da librerie esterne.

La preparazione copia le righe selezionate in512 byte locali; gli store
non rileggono testo/font. Ricerca bounded O(scalar*glifi), al massimo8192
confronti. Overflow, input/font errati, colori con byte alto e mapping
invalido: nessuno store. Fallimento del backend/readback/autorita dopo
l'inizio: errore con possibile risultato parziale, nessun rollback pixel.

## Accettazione

Test positivi RGB/BGR, stride/padding, limiti4096/64 celle, UTF-8 completo,
italiano e glifi mancanti; test negativi senza store; guasti dopo uno store
distinti. Contare le letture tabelle: rettangolo4096 e testo512 devono
richiedere lo stesso scan del singolo pixel. ASan C e accounting distinti
dal Rust e dalla RAM fisica. Dieci boot diagnostici finali: PTE/guardie,
rettangoli e tutti i pixel del testo verificati fisicamente e nel PPM.
Regressioni ordinarie con/senza video distinte.

## Risultati host e review

Quindici casi Rust distinti per profilo debug/release e Clippy: i quattro
nuovi casi includono il roundtrip di tutti gli scalar Unicode e il rifiuto
dei controlli, oltre a overlong, surrogate, troncamenti e confini geometrici.
Sette gruppi lifecycle x100, tre gruppi pixel e tre gruppi drawing x100
passano in C MSVC /W4 /WX con ASan. RGB/BGR, padding, offset4092,4096 pixel,
64 celle e256 byte effettivi, font ripetuti/disordinati, fallback, errori senza store e guasti
parziali simulati verificati. Contatore delle letture PTE uguale per singolo
pixel, rettangolo4096 e testo512. Budget allocatore ripristinato al cleanup.

Log: kernel/build/framebuffer-drawing-host.log. ASan strumenta C host,
non Rust o RAM fisica; Valgrind non disponibile. Nuove funzioni senza heap
o allocazioni fisiche; le tabelle live restano possedute come nel mapper.
Coverage linee/branch N/D. Cppcheck13 TU: zero error/warning/performance/
portability,12 note style e10 information. Le note sul readback volatile
non sono warning del compilatore e non giustificano rimuovere gli accessi.
Build finali diagnostica/ordinaria prive di warning. Disassembly in
kernel/build/framebuffer-drawing-disassembly.log: store/readback effettivi
MOV dword ptr [rax],ebx / MOV ecx,dword ptr [rax]; SIMD solo per RAM locale.

## Risultati firmware

Dieci boot diagnostici consecutivi finali raggiungono KERNEL_READY,
senza fault inattesi. Monitor QEMU legge768 foglie, guardie, CR3, PAT UC,
budget e2245 pixel fisici, incluso byte riservato; il PPM conferma tutti
gli stessi colori. Il nuovo raster comprende1536 pixel di rettangoli e704
di testo, oltre ai cinque pixel precedenti. QEMU BGR1024x768, BSP/TCG;
RGB e altri offset solo host, nessuna prova su hardware fisico o SMP.
I bitmap del diagnostico sono originali: OS:, spazio, sei accenti italiani;
euro valido ma assente dal font usa il quadrato Missing.

Diagnostico: kernel/build/framebuffer-live-3bd11e6947444d11a8f828203cc644d4/results.json.
SHA256 kernel:
A205EC845628F220E135DD840235563D8F4CE2E0D66D5CBE7152B6B3A521FB2D.
Ordinario: kernel/build/framebuffer-live-9d1cf95cb0d2400b9473aec3f916645a/results.json,
dieci boot senza probe di disegno.
SHA256 kernel:
CCB147EC833F3058F626F604CBAABDDA02B8BAF2EB7AF82E7827F5E4876F2EFC.
SHA256 loader invariato:
EB2398C1D09EDF333AD13BDDBFA915D591C5DA236D3900AE5E336685B34B67D7.
Senza display: kernel/build/framebuffer-live-b5d26ad255b5426d8cef014a9a8a5653/results.json,
dieci boot consecutivi sulla stessa immagine ordinaria; descrittore assente,
nessun mapping/store video. Le tre serie finali usano hash verificati e
loader invariato. Prestazioni/latenza IRQ non misurate.

Manifest finale: kernel/build/framebuffer-drawing-review.json, nessun
sorgente oltre400 righe o cartella funzionale oltre cinque file diretti;
whitespace, separatori, link e diff verificati. Nessun claim coverage,
hardware fisico, SIMD video o prestazione universale.
Riepilogo macchina: kernel/build/framebuffer-drawing-final.json; max286
righe sorgente. I test host aggiunti per256 byte non entrano nell'immagine
kernel: hash finali confrontati di nuovo con tutte e tre le campagne.

Riproduzione dalla root:

```powershell
./arch/x86/kernel/drivers/video/framebuffer/scripts/test.ps1
./kernel/scripts/build.ps1 -TestFramebufferMapping -OutputDirectory kernel/build/drawing-check
python ./arch/x86/kernel/drivers/video/framebuffer/scripts/firmware.py --kernel kernel/build/drawing-check/kernel.elf --rollback --pixels --drawing --boots 10
```

L'immagine diagnostica non si installa nella ESP ordinaria. La GUI, un
font completo e il collegamento alla shell sono blocchi successivi; questo
modulo fornisce primitive bounded, non un desktop o un terminale completo.

ESP aggiornata con l'immagine ordinaria verificata, hash della copia
confrontato. Backup:
kernel/build/framebuffer-drawing-previous-e956fefd5ee34d7e83760785db79f4cc.elf.
Log installazione: kernel/build/framebuffer-drawing-install.json.
