# Editor di riga PS/2 — progetto e verifica v0, 9 ottobre 2026

Snapshot precedente alla task shell. Per il collegamento attuale con FIFO
e consumer separato, compreso rifiuto delle righe troncate, vale [shell.md](../../../shell/documentation/shell.md).

Scelta approvata: 256 byte UTF-8 utilizzabili; rifiuto del nuovo carattere
se non entra interamente. Backspace rimuove uno scalar completo; Enter
consegna sincronicamente alla task consumer e poi svuota. Tab resta evento.
Nessuna shell, cursore, cronologia, completamento o nuova architettura IRQ.

## Ricerca e decisione

[xv6-riscv console.c](https://github.com/mit-pdos/xv6-riscv/blob/riscv/kernel/console.c),
branch riscv consultato il 9 ottobre, e un riferimento per la separazione
editing/lettura e il buffer limitato. Il nostro modulo non copia quel codice:
la coda IRQ e gia separata, il testo e Unicode e la consegna iniziale e
sincrona nello stesso consumer. Nessuna nuova dipendenza o codice importato.
[Unicode 17 §3.9](https://www.unicode.org/versions/Unicode17.0.0/core-spec/chapter-3/)
definisce la validazione della sequenza finale: lead/continuation, shortest
form e scalar valido. La v0 la implementa con interi e char::from_u32;
from_utf8 e stato valutato, ma richiedeva helper core non collegati dalla
toolchain freestanding attuale. Backspace riguarda scalar,
non grapheme cluster: accenti combinanti/compose restano fuori dalla v0.

## Ownership e invarianti

C possiede un buffer 257 byte (256 dati + NUL), lunghezza e durata. Lo stato
deve essere inizializzato e modificato esclusivamente dalle funzioni del
modulo, nella task a IF=0. Rust riceve solo lunghezza, ultimi quattro byte
impacchettati e azione/scalar: nessun puntatore, borrow FFI, heap o globale.
Il prefisso UTF-8 e valido per costruzione; Rust valida il carattere finale,
non certifica l'integrita arbitraria dell'intera RAM del kernel.

Schema NON eseguibile:

```text
evento -> decoder -> modificatori/layout -> scalar o azione
 -> editor: append completo | backspace scalar | full senza modifica
 -> Enter: buffer ancora posseduto -> consegna riuscita -> clear
 -> errore consegna: conserva buffer -> fail-stop
LOSS/malformed/stop: clear del parziale prima di chiudere il consumer
```

C controlla bounds/terminatore prima di leggere la coda del buffer e verifica
nuova lunghezza/status prima di scrivere. Append preserva NUL; backspace
azzera tutti i byte rimossi. Clear azzera anche il contenuto precedente.
La task non conserva puntatori oltre la chiamata di consegna. Future code
asincrone richiederanno un contratto distinto di copia/generazione/overflow.

La consegna v0 e osservabile su PS2_LINE_READY/PS2_LINE_LENGTH. Non e ancora
una API di shell o una coda verso un'altra task. Il logger corrente richiede
NUL entro256 byte: il consumer emette blocchi da128 byte senza aggiungere
separatori. Anche uno scalar diviso fra due blocchi conserva i byte UTF-8.
Il log diagnostico PS2_TEXT conta i caratteri ricevuti, inclusi i rifiutati;
il contenuto effettivo editato e quello di PS2_LINE_READY.

## Failure e minacce

- Overflow input: append atomico per scalar, FULL osservabile, stato invariato;
  una cancellazione consente subito nuovo testo. Nessuna attesa aggiunta.
- Stato/UTF-8 finale invalido: INVALID, buffer invariato; integrazione fail-stop.
- Input di controllo: rifiutato, nessuna sequenza ANSI o controllo raw emesso.
- Esc e LOSS: parziale scartato esplicitamente, nessuna riga consegnata per errore.
- Enter vuoto e valido: consegna lunghezza zero e riuso dello stesso buffer.
- Buffer/task consumer singoli; IRQ non legge/scrive l'editor. Nessuna promessa
  di sincronizzazione SMP, user copy, DMA o ring 3.

## Accettazione prima di dichiarare pronto

Test Rust positivi/invalidi, tutti gli scalar UTF-8 e confini 0/255/256;
test C/Rust con ASan, canary, rifiuto senza scritture, clear e riuso ripetuto;
build warning come errori e analisi statica. QEMU: dieci boot dell'immagine
finale, righe esatte dopo accenti/Backspace/Enter, Enter vuoto e overflow,
idle/IRQ/cleanup/budget. Prove effettivamente eseguite riportate sotto.

## Validazione finale

Immagine input ELF SHA256:
`738B6D481886C1581418410CA37F2B927FF73F2C8A9CEAFA7415F303F09DFD86`.

- [Host finale](../../../build/ps2-decoder-b6ee3fe38fac408eaf83f5f878072318/tools.log):
  20 casi Rust sia debug sia release (11 decoder,5 layout,1 UTF-8,3 editor).
  L'editor verifica tutti gli scalar da1 a110000, confini e cancellazione
  del carattere finale anche dopo un prefisso. Clippy e rustc con warning
  come errori. Test ABI editor C/Rust: 2680276 CHECK, 10000 cicli di riuso,
  canary, cancellazione a2/3/4 byte, buffer pieno, rifiuto senza modifica,
  stato/terminatore invalido, clear e Enter che conserva fino alla consegna.
  MSVC W4/WX + ASan nei test C; Rust safe non strumentato ASan.
  Le suite precedenti restano verdi: ABI testo1114132, controller6722,
  discovery7989, runtime4559 controlli (hardware host mock).
  Cppcheck15 file: zero error/warning/performance/portability;
  note style/information distinte dai diagnostici bloccanti.
- [Dieci boot finali](../../../build/ps2-input-firmware-25d83275bf2a42458fafe134eaaf55c1/results.json):
  10/10 consecutivi q35/TCG/OVMF/BSP, immagine finale verificata nell'ESP.
  Confronto esatto delle tre righe `abAAAaèéà@#€[`, vuota e `ciao`,
  con lunghezze18/0/4 byte. Backspace cancella anche l'accento completo.
  Ogni boot verifica IF=1 durante IRQ task, idle HPET, Esc, raccolta stack,
  chiusura route/coda/token e KERNEL_READY. Budget20468 pagine prima/dopo.
  Nessun fault inatteso o triple fault rilevato dal controllo del trace.
- [Stress esteso](../../../build/ps2-input-firmware-ba04e53e851c44b6a1aea4ae31329f04/results.json):
  stessa immagine,559 press/558 release,532 caratteri ricevuti,5 righe,
  un FULL,1518 IRQ durante task e38 idle. Dopo256 A viene rifiutato B:
  la riga consegnata contiene ancora256 A. Seconda riga da256 byte:
  127 A + è +127 A; l'accento attraversa il confine128 dei blocchi di log,
  verificato come UTF-8 esatto. Budget e cleanup ripristinati.
- [Regressione ordinaria](../../../build/ps2-decoder-firmware-77a29cc228bc4a9d8e2ea47fd2781557/results.json):
  10/10 boot, immagine distinta SHA256
  `15A08DF99A2748F994D9FE7060E8067B4668D388F3EE937941AB1E0F0421623C`.
  L'editor e collegato solo con Ps2Input; nessuna modifica al codice ordinario
  tra questa regressione e la correzione finale del consumer input.
- [Manifest finale](../../../build/ps2-line-source-final.json): hash e righe
  del componente e del suo script di build. Sorgenti massimo230 righe,
  script massimo320, cartelle funzionali massimo5 file diretti.
  Nessun trailing whitespace o CR isolato; diff e link locali verificati.

### Problemi trovati e corretti

1. from_utf8 richiedeva helper Rust core non risolti nel link freestanding.
   Sostituito con validazione intera safe della sequenza finale, verificata
   esaustivamente. Non sono stati introdotti runtime Windows o dipendenze.
2. Il test di byte invalidi provocava un warning MSVC sui cast di costanti
   char; fixture corretta, W4/WX senza esclusioni.
3. [Stress iniziale fallito](../../../build/ps2-input-firmware-d734bc4dc87e44309436fcacc2b15eef/boot-1/serial.log):
   kernel_text emetteva256 byte, poi rifiutava la stringa perche non aveva
   incontrato NUL nel proprio limite. Il consumer restava in fail-stop e
   non confermava la consegna ne svuotava la riga. Corretto con blocchi limitati nello stesso
   modulo input. Dieci boot e stress finali usano la nuova immagine; i boot
   della prima immagine non sostituiscono queste prove.

### Limiti della prova

Nessun heap aggiunto: buffer nella sessione C e array locali limitati.
ASan host non dimostra zero leak fisici; accounting copre la sessione
verificata. Valgrind, coverage linee/branch, hardware fisico, SMP e ring3 N/D.
Le failure del logger dopo una consegna parziale non sono state iniettate
sulla nuova immagine: il codice conserva buffer/owner e fa fail-stop.
LOSS/overflow della coda IRQ restano verificati host, non indotti nei boot.
La v0 consegna sul log, senza echo/editing visuale, cursore o API di shell.
La sessione conserva le guardie del runtime e termina con Esc.

## Riproduzione

Host: `kernel/drivers/input/ps2/scripts/test.ps1`.
Build: `kernel/scripts/build.ps1 -Ps2Input`.
Dieci boot: `arch/x86/kernel/drivers/input/ps2/scripts/firmware.ps1 -Boots 10`.
Stress: firmware.py con `--image <kernel.elf> --boots 1 --text --line-stress`.
I processi QEMU sono posseduti e chiusi dal singolo harness.
Prossimo lavoro: rendere la consegna della riga un'interfaccia per la shell,
con contratto distinto se il destinatario sara un'altra task.
