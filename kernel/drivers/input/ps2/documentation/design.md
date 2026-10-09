# Tastiera PS/2: decoder e contratto del collegamento

Scelta dell'utente: tastiera PS/2 emulata da QEMU, decoder Rust e task
consumer. Questo documento descrive il contratto del parser puro scan
set2, implementato nel primo blocco. Il controller aggiunto nel blocco
successivo e descritto in [controller.md](controller.md); il parser non
esegue I/O o handler IRQ. `-TestPs2Decoder` collega le sole prove C/Rust;
`-TestPs2Controller` aggiunge discovery e negoziazione hardware.
Il successivo `-Ps2Input` collega IRQ/coda/task:
[runtime](../../../../../arch/x86/kernel/drivers/input/ps2/documentation/runtime.md),
[evidenze](../../../../../arch/x86/kernel/drivers/input/ps2/documentation/validation.md).

## Contratto del decoder

Una task possiede un `Decoder` tramite riferimento Rust esclusivo; non
viene condiviso con l'ISR. Nessun heap, `unsafe`, puntatore C o stato globale.
Ogni byte richiede lavoro costante e produce al massimo un evento.
L'interfaccia Rust pubblica e in `rust/decoder/set2.rs`; il confine C
e in `decoder.h`; il gateway ABI della pipeline e in
`kernel/input/integration/rust/ffi/ps2.rs`. Una transizione riceve stato e
byte come u32, restituisce un u64 con prossimo stato nella meta alta e
evento/status nella meta bassa. Nessun puntatore o enum Rust attraversa
l'ABI. Byte >255 e stati non riconosciuti vengono rifiutati senza
troncamento. Il consumer C conserva soltanto lo stato u32, inizialmente 0.

I codici sono identificatori fisici opachi: byte ordinario, oppure byte
con bit 8 per E0. Il parser riconosce sintatticamente 1..0x84; non promette
che ogni valore sia un tasto assegnato. Il consumer dovra usare una tabella
di mapping controllata, senza indicizzare array con dati non validati.
Pause e il codice 0x200 con azione Pulse: non genera un rilascio e non
deve diventare un tasto permanentemente premuto. Print Screen conserva
0x17c; i suoi Shift sintetici E0 12 / E0 F0 12 sono soppressi. Le varianti
brevi con Shift/Ctrl e SysRq con Alt conservano la semantica del flusso.
Layout, Unicode, stato modificatori e autorepeat non sono ancora gestiti.

Prefissi ripetuti o byte fuori grammatica producono Invalid e reset.
Il byte errato viene consumato, senza replay. Una sequenza Pause non
completa non produce eventi. LOSS dalla coda richiede reset del parser
e dello stato modificatori/pressed del consumer. Reset elimina il prefisso
precedente; non ricostruisce byte persi e non autentica il framing di
byte successivi. Dopo un errore del trasporto il futuro driver dovra
quiescere e risincronizzare la sorgente, con deadline, prima della ripresa.

## Fonti e limiti

Riferimento primario del protocollo emulato:
[QEMU v10.1.0, hw/input/ps2.c](https://github.com/qemu/qemu/blob/v10.1.0/hw/input/ps2.c),
funzione `ps2_keyboard_event`, ramo `scancode_set == 2`, e
`ps2_write_keyboard` per le risposte di comando. Protocollo studiato,
parser scritto indipendentemente: nessuna copia del codice QEMU.

Il binario installato dichiara 11.1.0 (v11.1.0-12130-ge470268ff4).
Il riferimento 10.1.0 non e una verifica del sorgente di quel binario:
il probe controller verifica ora selezione/query del set2 e traduzione
disattivata tramite readback. La ricezione effettiva dei tasti e ora
provata nel profilo IRQ esplicito. Nessun auto-detect
dal solo flusso: alcuni byte del set 1 sono sintatticamente compatibili.

Discovery prevista: FADT valida, IA-PC Boot Architecture Flags bit 1,
8042, secondo [ACPI 6.6 §5.2.9.3, tabella 5.11](https://uefi.org/specs/ACPI/6.6/05_ACPI_Software_Programming_Model.html#ia-pc-boot-architecture-flags).
La presenza del flag non dimostra tastiera connessa o funzionante.
IRQ1 viene risolto tramite MADT/override; nessuna route presunta.
Hardware fisico, USB e tastiere I2C fuori dal profilo attuale.

## Contratto del collegamento nello stesso componente

Schema non eseguibile:

```text
discovery -> controller disabilitato -> negoziazione con deadline
 -> binding evento + coda -> route IRQ -> abilita scanning
IRQ: stato valido -> lettura limitata -> push byte/loss -> acknowledgement/EOI
task: pop -> decoder -> mapping -> output; coda vuota -> observe/recheck/wait
stop: maschera route -> ferma sorgente -> drena -> chiudi coda -> unbind
```

Una sorgente, una task consumer, coda 256 byte con rifiuto del nuovo byte
e barriera LOSS: riusa le decisioni gia approvate. ISR senza decoder,
allocazioni o logging. Task con budget per non monopolizzare il FIFO.
IRQ durante task secondo il profilo gia scelto; mutazioni protette a IF=0.
Assenza/timeout della tastiera deve lasciare una ragione osservabile;
nessuna attesa illimitata e nessun successo dichiarato dopo una risposta
non verificata. Il watchdog indipendente resta prerequisito dell'idle.

Accettazione eseguita nel blocco runtime: input nella VM posseduta,
identificatori fisici e release, stop/cleanup e dieci boot consecutivi.
Perdita provata su coda/ISR host, non indotta sul device reale QEMU;
riavvio della sessione non implementato. Le sole prove del parser con
byte sintetici non sostituiscono quelle hardware del collegamento.

Il mapping del testo italiano e ora separato dal parser dei tasti fisici:
[contratto testo](../../../../input/keyboard/documentation/text.md). La task possiede lo stato dei modificatori;
IRQ, coda e decoder conservano il contratto precedente.
