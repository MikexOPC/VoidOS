# Testo italiano PS/2 — contratto e verifica, 9 ottobre 2026

Snapshot del blocco testo precedente all'editor. Immagini e conteggi restano
evidenze di quel blocco. Il [contratto editor](../../../console/line/documentation/editor.md) descrive il percorso
attuale con buffer di riga, Backspace e consegna su Enter.

Scelta dell'utente: italiano con lettere accentate e AltGr. Il profilo
esplicito `-Ps2Input` traduce ora gli eventi fisici nella task consumer;
la build ordinaria resta diagnostica. `reuse/` non viene modificato.

## Contratto v0

- Rust safe/no_std possiede la transizione dei modificatori e il mapping
  italiano. Stato passato per valore, nessun puntatore FFI, heap o globale.
  C possiede lo stato della singola task e il buffer di output.
- Shift sinistro/destro e Ctrl sinistro/destro sono distinti. Caps Lock
  cambia A–Z; Shift e Caps combinati invertono il caso. Le lettere accentate
  sui tasti dei simboli non cambiano con Caps, secondo la tabella scelta.
  Un make ripetuto di Caps non lo commuta nuovamente prima del break.
- Alt destro significa AltGr. Sono disponibili euro, @, #, parentesi quadre
  e, con Shift, graffe. Ctrl e Alt sinistro sopprimono testo: Ctrl+Alt non
  e un alias di AltGr nella v0. I release non generano caratteri.
- Enter, Tab e Backspace sono azioni tipizzate fuori dall'intervallo Unicode,
  registrate come eventi. Non vengono inviati al terminale come controlli;
  non sono ancora un editor di riga. Esc ferma la sessione indipendentemente
  dal layout. Pause, Print Screen e altri tasti non testuali non emettono testo.
- ABI intera: risultato u64, stato/length nei 32 bit alti, scalar/byte UTF-8
  nei bassi. Un evento o stato malformato restituisce INVALID e stato zero.
  I byte UTF-8 sono impacchettati in ordine little-endian, fino a quattro.
  C controlla lunghezza e caratteri di controllo prima della copia in un
  buffer locale di cinque byte, inizializzato a zero e terminato con NUL.
- LOSS o input malformato azzera parser e modificatori e richiede stop:
  non viene dichiarata una ripresa della sessione. Nessun lavoro di testo
  aggiunto nell'IRQ; rimangono ownership, yield e cleanup del runtime.

Sono esclusi keypad/Num Lock, LED, compose/dead keys, configurazione dinamica,
repeat temporizzato, editor, shell e sessione illimitata. Make ripetuti delle
lettere producono testo; non viene introdotto un timer di autorepeat.

## Fonti e metodo

- [Microsoft Italian, KLID 00000410](https://learn.microsoft.com/en-gb/globalization/keyboards/kbdit)
  e [tabella interattiva ufficiale](https://learn.microsoft.com/en-us/globalization/keyboards/kbdit.html),
  viste normal/shift/caps/ctrlalt/shiftctrlalt. Mapping scritto autonomamente
  dai valori VK/U+, senza copiare o eseguire JavaScript Microsoft.
  Copia HTML consultata: `kernel/build/ps2-italian-reference.html`, SHA256
  `45EF87A71D24AAC98F62E2AEEC7DC84285C025052819D786815DE181AB34826F`.
- [QEMU keycodemapdb, data/keymaps.csv](https://github.com/qemu/keycodemapdb/blob/master/data/keymaps.csv),
  consultato il 9 ottobre: identita dei tasti fisici e scan set 2; riferimento
  upstream mobile, non dipendenza del build. Il test firmware verifica
  effettivamente i nomi sendkey e gli eventi del QEMU installato.
- [Unicode 17, §3.9, tabella 3-7](https://www.unicode.org/versions/Unicode17.0.0/core-spec/chapter-3/):
  UTF-8 valido e shortest form, esclusi surrogate e valori oltre 10FFFF.
  Rust valida lo scalar con char::from_u32 prima di encode_utf8.

Nessun nuovo registro, accesso port I/O, MMIO o Assembly. Per controller,
route e entry IRQ valgono le fonti del [runtime](../../../../arch/x86/kernel/drivers/input/ps2/documentation/runtime.md).

## Prove eseguite sulla modifica finale

Input ELF SHA256:
`BCDD93107AAEEFA4958FCA10F6E33F72B4CFE894E6F5742E4E2A08915DBC85F6`.

- [Host](../../../build/ps2-decoder-cd0326b9d7b6466fb53da92bf10bcdaa/tools.log):
  11 decoder + 5 layout + 1 UTF-8, tutti passati sia debug sia release.
  Layout: 657920 combinazioni stato/codice/azione, Shift indipendenti,
  Caps ripetuto, accenti, AltGr e invalidi. UTF-8: tutti i valori da zero
  a 110000 inclusi, con roundtrip e fixture indipendenti per accenti/euro.
  ABI C/Rust: 1114132 controlli. Controller 6722, discovery 7989,
  route/IRQ runtime 4559 controlli: hardware di questi test host mock.
  MSVC W4/WX e ASan nei test C; Rust non strumentato ASan, core safe.
  Clippy con warning come errori; cppcheck su 13 file senza diagnostiche
  error/warning/performance/portability. Restano note style/information.
  Corretto l'incremento dentro assert trovato dall'analisi del nuovo test.
- [Dieci boot italiani](../../../build/ps2-input-firmware-ec40fb097d4846b1aede69044c66039a/results.json):
  10/10 consecutivi, q35/TCG/OVMF/BSP. Sequenza Unicode e byte UTF-8
  confrontati esattamente, Shift su entrambi i lati, Caps, accenti, AltGr,
  Ctrl senza testo e tre eventi editing. Per boot 35 press, 33–34 release,
  14 caratteri, 9–22 IRQ durante task e 24–35 idle supervisionati.
  Budget fisico 20468 pagine prima/dopo, cleanup confermato e KERNEL_READY;
  nessun fault inatteso o triple fault rilevato dal controllo del trace.
- [Stress](../../../build/ps2-input-firmware-a48438900e26440e9fa280479f1c63ce/results.json):
  stessa immagine, 100 tasti A aggiuntivi, 135 press/134 release,
  114 caratteri confrontati, 294 IRQ in task, 50 idle, stesso budget.
- [Dieci boot ordinari](../../../build/ps2-decoder-firmware-09808837553b464e89d3b52b9a2f9091/results.json):
  10/10, immagine distinta SHA256
  `AD2025910A867DDD939A14B8EB863B57C7544CC71F85F70EE1D7B1C88920B7CA`.
  Regressione dei marker, stato CPU/memoria e cleanup del profilo ordinario.
- [Manifest sorgenti](../../../build/ps2-text-source-final.json): dimensioni
  e hash. Massimo 230 righe nei sorgenti del modulo, cinque file diretti;
  nessun trailing whitespace/CR isolato. Diff verificato.

Il nuovo percorso testo non alloca heap. ASan host e accounting della
sessione non dimostrano assenza globale di leak RAM fisica. Valgrind,
coverage linee/branch, hardware fisico, SMP e uso ring 3 non misurati.
Overflow/LOSS restano verificati host, non indotti in questi boot QEMU.
La verifica precedente del collegamento resta uno snapshot separato.

## Riproduzione e prossimo blocco

Build: `kernel/scripts/build.ps1 -Ps2Input`; host:
`kernel/drivers/input/ps2/scripts/test.ps1`; dieci boot con testo:
`arch/x86/kernel/drivers/input/ps2/scripts/firmware.ps1 -Boots 10`.
Quest'ultimo harness inietta il layout italiano e verifica UTF-8 sul log;
accetta KernelDirectory/SkipBuild per un'immagine gia costruita.
Stress: lo script firmware.py accetta `--text --stress 100`.

Prossimo contratto da progettare: editor di riga a capacita fissa, azioni
editing e consegna alla task. Non viene implementato implicitamente qui.
