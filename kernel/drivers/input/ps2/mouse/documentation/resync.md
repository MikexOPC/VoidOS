# Mouse PS/2: tolleranza a perdite, header errati e overflow — 16 ottobre 2026

## Problema

Nella prova manuale in QEMU (`kernel/build/pointer-interactive-d569c27f35bd4185bf4327079c2a03b5/serial.log`)
la sessione e terminata con `PS2_INPUT_FAILED` dopo una sola perdita
(`MOUSE_LOSSES: 1`): puntatore scomparso e console chiusa senza righe
(`SHELL_TASK2_CLOSED_EMPTY`). Il task mouse trattava come fatali perdita
del payload, header senza bit 3 e pacchetti con overflow X/Y.

## Politica

- Perdita payload o header rifiutato: il decoder torna allo stato header, il
  byte viene saltato e l'errore e contato (`Losses`, `Rejected`). Un header
  richiede bit 3, quindi il frame si riallinea entro un pacchetto.
- Pacchetto con overflow (bit 6/7 dell'header): i tre byte sono consumati e
  l'evento intero scartato, come da [design](design.md); contato in `Saturated`.
  Il frame e integro: la serie di errori non cambia.
- La sessione fallisce solo dopo `X86_PS2_MOUSE_RESYNC_LIMIT` (64) errori di
  frame consecutivi senza un evento valido. 64 e un'ipotesi da misurare,
  non un valore hardware.
- `x86_console_finish` non respinge piu una chiusura per `Mouse.Losses`: restano
  gate `Failed`, decoder a riposo, pulsanti rilasciati, token e route rilasciati.
- Il report stampa anche `MOUSE_REJECTED` e `MOUSE_SATURATED`.

## Prove

Regressione prima/dopo con `test/kernel/drivers/input/ps2/mouse/runtime/task_test.c`
(13 scenari x 1000 cicli): sul codice precedente fallisce l'asserzione su
`Failed` (`kernel/build/ps2-mouse-task-before-*`); dopo la modifica passa con
1.728.000 controlli (`kernel/build/ps2-mouse-task-after-15f6b08d6de545419ddfabe7988f775c`).
Scenari: perdita fra pacchetti e dentro un pacchetto, byte di coda dopo una
perdita, header errato, 200 pacchetti in overflow, rumore sotto e sopra il limite,
64+1 perdite consecutive.

Suite host `ps2`, `console` e `x86-framebuffer` passano (C/ASan, Rust, cppcheck).
La suite `ps2` falliva prima su spazi finali gia presenti in tre file non toccati
(commit separato).

QEMU, immagine `-Ps2Input -Ps2Mouse -ConsoleShell` SHA256
`B9F88F9319175B57540526CDEE8C4FBF8CFD5CC72B32BB6A401B555901E9104D`:
10 boot consecutivi con mouse, puntatore, testo e shell
(`kernel/build/ps2-input-firmware-a6dc1112a2934ecd80c3fd7b99438c36`), budget
20415 ripristinato in tutti; un boot di stress da 200 tasti con mouse e
righe lunghe (`ps2-input-firmware-4e0b2c8ae3b14fbca3d781dba01d5338`); 10 boot del
profilo ordinario (`scheduler-firmware-932dde47493d4b3c858a322e7b430c0d`).

## Limiti

QEMU non produce perdite, header errati o overflow: la tolleranza e provata
sull'host con payload simulato, e in QEMU solo come assenza di regressioni.
La tastiera tratta ancora una perdita come fatale (`runtime/task.c`), e un
errore dell'ISR PS/2 ferma ancora il kernel. La causa della perdita osservata
(consumer lento per la stampa seriale di ogni evento) non e stata ridotta qui.
Hardware fisico non testato.
