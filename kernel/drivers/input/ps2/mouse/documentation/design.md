# Mouse PS/2 v0: decoder e contratto di integrazione

Scelta utente dell'11 ottobre 2026: movimento e tre pulsanti; rotella dopo.
Il primo blocco implementava solo il decoder e la sua interfaccia C.
Il blocco successivo aggiunge la preparazione AUX con il controller condiviso,
nel solo profilo `-TestPs2Controller`. Nessuna route IRQ12 o scrittura video.
Vedere [contratto e prove del trasporto](controller-validation.md).

## Fonti e profilo

[QEMU v10.1.0 ps2.c](https://github.com/qemu/qemu/blob/v10.1.0/hw/input/ps2.c),
`ps2_mouse_send_packet`, `ps2_mouse_event`, `ps2_write_mouse`: il dispositivo
base invia tre byte; header bit3 fisso, bit4/5 segni X/Y, bit0..2 pulsanti.
Il produttore emulato limita ogni delta a -127..127 e inverte Y rispetto
all'input relativo dello schermo. I protocolli estesi usano quattro byte.

[QEMU v10.1.0 pckbd.c](https://github.com/qemu/qemu/blob/v10.1.0/hw/input/pckbd.c),
definizioni KBD_STAT/KBD_MODE/KBD_CCMD e `kbd_write_data`: dati a porta0x60,
status/comandi a0x64; status bit5 distingue AUX, mode bit1 abilita IRQ12,
mode bit5 disabilita AUX, D4 instrada la scrittura successiva al mouse.
Questi riferimenti motivano l'integrazione futura, non accessi già implementati.

[Linux v6.12 psmouse-base.c](https://github.com/torvalds/linux/blob/v6.12/drivers/input/mouse/psmouse-base.c),
`psmouse_report_standard_motion`, `psmouse_report_standard_buttons` e
`psmouse_receive_byte`: confronto per segni, pulsanti e perdita di sincronizzazione.
Linux tratta il caso byte zero con segno impostato come zero; questa v0
mantiene invece la rappresentazione signed a nove bit, senza quella quirk.
È una scelta esplicita, non una compatibilità fisica certificata.
Nessun codice esterno è stato copiato.

Il QEMU installato dichiara11.1.0: la fonte studiata è10.1.0. Il round trip
sul binario installato resta un gate dell'integrazione hardware.

## Ownership e ABI

C possiede uno stato u32 per consumer e un evento distinto, valido e
scrivibile. Prestiti esclusivi, niente reentrancy/SMP; il controllo numerico
di overlap non verifica mapping o lifetime. Buffer sovrapposti o NULL
vengono rifiutati senza scritture. Nessun heap, MMIO, callback o globale.

Rust riceve solo interi: `step(state, byte)` restituisce u64. Lo stato
successivo occupa i bit32..63; il risultato occupa i bit0..31. Stato zero
significa header atteso. Tag1 conserva header nei bit8..15; tag2 conserva
anche X nei bit16..23. Tutti gli altri bit devono essere zero.

Risultato: X a nove bit in0..8, Y in9..17, pulsanti in18..20;
PENDING bit24, INVALID bit25, OVERFLOW bit26. Status senza payload;
evento senza status e stato successivo zero. INVALID ripristina stato zero.
Il gateway esporta solo l'ABI intera; il parser vieta unsafe.

Y positivo resta verso l'alto; la conversione in coordinate schermo
appartiene al futuro servizio puntatore. Pulsanti: bit0 sinistro,
bit1 destro, bit2 centrale. Delta nel contratto: -256..255, distinto
dal sottoinsieme prodotto da QEMU. Nessun cast C signed fuori intervallo.

## Stati ed errori

Schema non eseguibile: HEADER -> X -> Y -> EVENTO -> HEADER.
Header senza bit3 o byte oltre255: INVALID. Non interpretare FA/FE/AA
nei payload come risposte: il trasporto deve separarle prima del decoder.
Overflow header bit6/7: consumare i tre byte e rifiutare l'intero evento,
senza aggiornare pulsanti o posizione. Per byte valido: lavoro O(1).

Una perdita di coda, parità, timeout o pacchetto malformato richiede stop
del canale e drain verificato prima di una nuova sessione. Reset del decoder
non dimostra sincronizzazione: un payload può sembrare un header.
Il chiamante deve invalidare anche lo stato dei pulsanti. Non cercare
automaticamente il bit3 nel flusso dopo una perdita.

## Integrazione futura nello stesso modulo

Un solo owner del controller per tastiera e mouse; nessun secondo owner
che azzeri mode o chiuda il controller mentre la tastiera è attiva.
Comandi serializzati prima dello streaming; separare risposta AUX,
risposta controller e scancode. Entrambi gli handler devono instradare
il byte in base allo status letto, non al solo vettore ricevuto.

Due code e due eventi distinti, una sorgente hardware per evento con
generazione. Riutilizzare i limiti già scelti:256 byte, rifiuto del nuovo
byte e barriera di perdita. Decodifica nella task; IRQ senza rendering,
allocazioni o log. IRQ12 risolto tramite MADT, non GSI presunto.
Senza mouse supportato, mantenere la tastiera operativa.

Ordine di completamento: trasporto condiviso e prepare/stop AUX;
binding IRQ/task e eventi; test combinati mouse+tastiera e cleanup.
Il puntatore grafico richiederà poi un contratto del renderer separato.

## Gate

Test host esaustivi dei delta/pulsanti, stati corrotti, overflow, reset,
payload ambigui, overlap e ABI; warning come errori, Clippy, ASan C,
cppcheck. Dopo integrazione: almeno dieci boot finali con input combinato,
assenza mouse, guasti intenzionali, deadline e budget risorse ripristinato.
Fino a quel gate il driver mouse non è pronto e non si avanza al puntatore.
