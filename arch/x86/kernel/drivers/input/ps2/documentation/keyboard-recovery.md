# Recupero della tastiera PS/2 (B2) — 18 ottobre 2026

Prima: una perdita nel payload, un byte rifiutato dal decoder, un errore di parita/timeout
dell'i8042 o un burst oltre 32 byte fermavano la sessione o il kernel. Ora sono errori
recuperabili; restano terminali solo le violazioni di ownership/protocollo.

## Classificazione

| Evento | Prima | Ora |
|---|---|---|
| Marcatore LOSS nel payload | `Failed`, `Stop` | resync, `Streak++` |
| Decoder INVALID | `Failed`, `Stop` | resync, `Streak++` |
| Status i8042 bit 6/7 (bit 5 senza mouse) | `Error`, ISR ritorna 0, kernel fermo | byte letto e scartato, `DeviceErrors++`, EOI, resync al byte successivo |
| Burst oltre 32 byte | `Error`, kernel fermo | scarto fino a `X86_PS2_DRAIN_LIMIT` (64) letture, `Discarded += n`, `DeviceErrors++`, EOI |
| `payload_push` risultato 0, payload non DATA/LOSS, contatori a `~0U`, task non valido | terminale | terminale (invariato) |

Resync (`x86_ps2_fault_resync`): azzera `Decoder` e `TextState` (i modificatori vivono li, quindi
nessun tasto resta premuto), svuota la riga e `LineTruncated`, incrementa `Resyncs`; il
consumer rinfresca poi la riga della console. Un evento tastiera valido azzera `Streak`
(`x86_ps2_fault_valid`). Se `Streak` supera `X86_PS2_KEY_RESYNC_LIMIT` (64, ipotesi da misurare
come per il mouse) la sessione termina pulita con `Failed`, `Stop` e `Losses++`.
`Losses` resta quindi il contatore delle perdite *terminali*; quelle recuperate sono `Resyncs`.

Le perdite di byte nell'ISR non generano un payload: il consumer le vede confrontando
`DeviceErrors` con `DeviceSeen` alla prossima lettura. Se nessun byte arriva piu, lo stato
parziale resta fino al tasto successivo.

## Migrazione verso "lasciare i byte al worker" (alternativa B)

La politica e isolata in `drain_private` (`runtime/interrupt.c`): oggi legge e scarta. Passare
a B significa sostituire solo quel corpo (lasciare i byte, o accantonarli) e aggiungere la
lettura del worker con la regola di esclusione ISR/worker; consumer, contatori e resync non
cambiano. `Discarded` e il dato per decidere: se in uso reale resta 0, B non serve.

## Fonti e limiti

- Bit di status dell'i8042 (6 = timeout, 7 = parita, 5 = AUX): come in `interrupt.c`; QEMU
  (`pckbd.c`) non li genera mai. Comportamento "leggi sempre il byte quando OBF e impostato,
  anche con errore" ispirato al driver i8042 di Linux (logica, non codice copiato). **Non
  verificato su una fonte primaria ne su hardware reale**: il datasheet del controller non e stato
  consultato in questa sessione.
- Non provato in QEMU: nessun fault arriva dall'hardware emulato. Coperti da test host: ISR con
  porte simulate (0..200 byte, bit 5/6/7 con e senza OBF, push 0/1/2, EOI fallito), politica
  `fault_poll/note/valid/resync` (limite 64, saturazioni). Il consumer end-to-end con un fault
  reale non e esercitato; QEMU verifica che in una sessione sana `PS2_RESYNCS`,
  `PS2_DEVICE_ERRORS` e `PS2_DISCARDED` restino 0.

## Prove

Suite host (12 su 12; test ps2 runtime 53695 e 53757 controlli con e senza mouse), QEMU tutti
exit 0: 10 boot mouse+puntatore+shell, stress da 200 tasti, 10 boot tastiera
(`kernel/build/b1-campaign/b2`).
