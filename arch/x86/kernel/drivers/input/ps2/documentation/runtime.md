# Input PS/2 collegato alle task — v0 QEMU

Scelta gia approvata: tastiera PS/2 QEMU, decoder Rust, una sorgente per
evento e un consumer. Il profilo esplicito `-Ps2Input` implementa IRQ1 →
coda Rust → task → decoder Rust → layout italiano → UTF-8 sul log seriale.
Esc annulla la riga e il flag di troncamento, senza chiudere la sessione o
modificare Shift/Ctrl/Alt/Caps Lock. Enter consegna la riga UTF-8 completa alla task;
Backspace cancella uno scalar e Tab resta evento. Il buffer contiene al
massimo256 byte, con rifiuto del nuovo carattere se pieno. La task shell2
riceve copie delle righe tramite FIFO4: help, echo, status ed exit.
Una riga con overflow viene rifiutata come comando. [Contratto shell](../../../../../../../kernel/shell/documentation/shell.md);
[contratto editor](../../../../../../../kernel/console/line/documentation/editor.md);
il [report testo](../../../../../../../kernel/input/keyboard/documentation/text.md)
resta lo snapshot del blocco precedente.

## Ownership e transizioni

La sessione C possiede controller, route, token del registro IRQ e token
generazionale dell'evento4. La task0 e l'unico consumer dei byte; la task1 mantiene
la finestra Assembly verificata a IF=1 e poi attende l'evento. Mutazioni
del dispatcher e pop/decoder restano a IF=0. Si tratta di BSP cooperativo,
non preemption o esecuzione C arbitraria con IRQ abilitati. La task2 shell
esegue a IF=0, con eventi software5/6 dati/spazio e senza borrow dell'editor.

Il LAPIC timer viene fermato, scollegato e ripristinato prima di riusare
il suo slot/vector41. HPET conserva vector40 e protegge l'idle HLT.
L'attesa vuota e un heartbeat normale, distinto dal timeout di un evento
obbligatorio. Ogni heartbeat richiede realmente l'ack HPET, finestra idle
e stato LAPIC valido: il successo non deriva dall'assenza di log.

```text
FADT/MADT/capacita verificate
 -> controller PREPARED -> route acquisita masked
 -> evento/coda/handler -> tre stack pinned -> prestito watchdog48 pagine
 -> scanning F4 con ACK -> IRQ1 abilitato -> route unmasked
IRQ -> OBF/read limitato -> push DATA o LOSS -> EOI
task0 -> pop -> decoder -> modificatori/layout/UTF-8 -> editor -> FIFO righe; EMPTY -> observe/recheck/wait
task1 -> observe -> Assembly IF=1 con verifica GPR/SIMD -> wait/yield
task2 -> observe dati -> pop copia -> signal spazio -> builtin -> clear/yield
stop exit -> mask -> F5/close -> ack IRR/HPET -> unbind handler/restore route
 -> sveglia worker e producer -> drena byte -> chiudi FIFO/EOF -> termina prestito -> collect tre stack
 -> close coda/unbind evento -> budget ripristinato -> close HPET
```

Il consumer cede la CPU fra eventi. DATA/LOSS/EMPTY e generazioni sono
gestiti dalla coda gia verificata; ISR senza decoder, log o allocazioni.
Guasti isolati e LOSS azzerano parser, modificatori e riga secondo il
[contratto di recupero B2](keyboard-recovery.md); un guasto ISR scarta prima i byte
incerti. Superare il limite di guasti consecutivi resta terminale. Cleanup non
confermato, ownership o contesto invalido conservano gli owner, senza liberare
mapping o stack ancora potenzialmente usati. La v0 non riavvia una sessione chiusa.

Il consumer azzera anche la riga parziale su LOSS, input malformato e stop.
Enter conserva il buffer fino alla consegna sincrona riuscita; il log
PS2_LINE_READY mostra la riga editata e PS2_LINE_LENGTH la lunghezza in byte.
La consegna alla shell e una copia, pubblicata prima del clear dell'editor;
se FIFO piena la task0 attende spazio conservando la riga. Exit usa lo stop
con cleanup, senza eseguire comandi pendenti dopo la richiesta.
Il log PS2_TEXT resta diagnostica dei caratteri ricevuti, inclusi quelli
rifiutati dall'editor quando pieno; non e un echo interattivo della riga.

Il poll Assembly restituisce0 per rifiuto/corruzione,1 per dati,2 per
attesa vuota con registri e SIMD integri. Il vecchio ingresso timer
conserva la semantica0/1. Poll massimo100ms e limite20milioni iterazioni;
sono budget di bring-up, non benchmark o timeout universali hardware.
B1 elimina il limite globale di dispatch/idle per la sessione input: restano
finite e sorvegliate le singole attese. Esc svuota l'editor e pubblica
PS2_EDIT_CANCEL sia alla pressione sia al repeat; il rilascio non annulla di nuovo.
Non invia righe alla shell, non incrementa il contatore dei recuperi da guasto e
aggiorna la console tramite la normale API serializzata. Exit richiede lo stop.

## Esc come annullamento — verifica del 19 ottobre 2026

Il consumer svuota l'editor tramite `kernel_ps2_line_clear`, azzera solo
`LineTruncated` e aggiorna la console con `x86_console_edit` prima di pubblicare
PS2_EDIT_CANCEL. Restano invariati stato dei modificatori, contatori di guasto,
code mouse/shell e ownership. Fallimenti della console o del log restano terminali.
I test usano ora exit per richiedere il cleanup; gli helper monitor/seriale condivisi
sono in `scripts/protocol.py`, senza cambiare la creazione delle VM possedute.

- Host: 277 scenari consumer per variante seriale/video, MSVC /W4 /WX e ASan;
  riga vuota, UTF-8 e piena/troncata, repeat/rilascio Esc, modificatori conservati,
  nuovo comando dopo annullamento e regressioni del recupero B2.
  Suite C/Rust e cppcheck passati: `kernel/build/escape-cancel-host-final.log`,
  evidenze `kernel/build/ps2-decoder-40e6f26b4d334027a6826fed5134f189`.
- Immagine finale `92C5095EE3BB43B0DF8994B8520AA42F0374BB55BA11E7C0C0DD47D2B3E4EAA9`:
  dieci boot consecutivi, 120 scene di movimento e dieci scene dopo Esc corrette,
  testo exit annullato, help successivo, mouse attivo e cleanup tramite exit.
  Budget fisico ripristinato. Directory
  `kernel/build/pointer-interactive-check-94a65626bbe44847b168e0d62cf9e236`.
- Due smoke del profilo solo tastiera, senza/con testo italiano, passati:
  `kernel/build/escape-cancel-keyboard-qemu.log` e
  `kernel/build/escape-cancel-keyboard-text.log`.
- ESP del launcher verificata con annullamento Esc e cleanup tramite exit:
  `kernel/build/escape-cancel-launcher-smoke.log`.

Prima della ricompilazione del launcher erano gia passati dieci boot sull'immagine
`8563774DBBC5A1C8D56376219D6467B7AB851B7AA0D586F69A3EA19F84D98667`.
Il confronto rileva soltanto due byte di timestamp nella directory debug, segmenti
eseguibili identici (`kernel/build/escape-cancel-rebuild-comparison.json`). La campagna
finale e comunque associata all'hash esatto del binario rigenerato.

Non e una correzione del renderer: audit finale delle catture durante il movimento,
153 complete, cinque parziali, nessuna assente; le scene assestate passano.
ASan e accounting QEMU sono prove distinte; Valgrind non eseguito, coverage non misurata,
nessuna nuova prova su hardware fisico.

## Registri e fonti hardware

- [ACPI6.6 FADT, §5.2.9.3](https://uefi.org/specs/ACPI/6.6/05_ACPI_Software_Programming_Model.html#ia-pc-boot-architecture-flags):
  campo IAPC_BOOT_ARCH a109, bit1 8042. Nessun port I/O se assente.
- MADT ISA override Source1 risolve GSI/polarita/trigger. Conformi ISA
  significano high/edge; valori riservati2, alias ISA e NMI sono rifiutati.
  Indirizzo IOAPIC, ID, versione e pin derivano da discovery/capacita reali.
- [Intel82093AA 290566-001 §3.1.1/3.1.2/3.2.4](https://cs.nyu.edu/~mwalfish/classes/ut/s10-cs372h/ref/ia32/ioapic.pdf):
  IOREGSEL offset0, IOWIN10h, accessi32bit; RDTlow selector10h+2*pin,
  high+1. Vector7:0, polarity13, trigger15, mask16; delivery12/remoteIRR14
  sono read-only e devono essere clear prima della sostituzione.
  Il chip originale ha destinazione fisica a4bit59:56; il profilo
  [QEMU v10.1.0 ioapic.c](https://github.com/qemu/qemu/blob/v10.1.0/hw/intc/ioapic.c)
  usa destinazione a8bit63:56. Il codice usa il BSP ID verificato<=255,
  high31:24, riservati a zero. Non e una promessa per ogni IOAPIC fisico.
- [QEMU v10.1.0 pckbd.c](https://github.com/qemu/qemu/blob/v10.1.0/hw/input/pckbd.c):
  porte byte60h/64h; status OBF0, AUX5, timeout6, parity7. Mode IRQ1bit0,
  disable keyboard4/aux5 e translation6. Mode query/write20h/60h verificati.
  [ps2.c](https://github.com/qemu/qemu/blob/v10.1.0/hw/input/ps2.c): F4 enable,
  F5 reset/disable e ACK FA; il controller prepara/query set2 con F0.
- PM clock e EOI usano helper gia verificati; SDM Vol3 §10.8.5 per EOI,
  Vol3 §7.8.1 e Vol2 CLI/STI/FXSAVE64 per la finestra Assembly.

QEMU installato dichiara11.1.0, build e470268ff4: sorgenti10.1.0 studiati
come riferimento, non dichiarati identici al binario. Readback e tasti
iniettati verificano il comportamento del dispositivo effettivo.

## Riproduzione

```powershell
& arch/x86/kernel/drivers/input/ps2/scripts/firmware.ps1
& kernel/drivers/input/ps2/scripts/test.ps1
```

Il primo comando costruisce un'immagine isolata, copia ESP/OVMF per VM,
inietta tasti tramite monitor QEMU e termina soltanto il processo posseduto.
Accetta `-Python` per scegliere un interprete; default runtime locale Codex.
`-SkipBuild` riusa l'immagine gia costruita. Nessun accesso alla tastiera
o al disco fisico dell'host. La build ordinaria resta diagnostica finita.

Prove e limiti misurati: [validation.md](validation.md).
