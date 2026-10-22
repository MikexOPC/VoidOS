# Recupero della tastiera PS/2 (B2) — 18 ottobre 2026

Prima: una perdita nel payload, un byte rifiutato dal decoder, un errore di parita/timeout
dell'i8042 o un burst oltre 32 byte fermavano la sessione o il kernel. Ora sono errori
recuperabili; restano terminali le violazioni di ownership/protocollo e il superamento
del limite di errori consecutivi.

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

## Correzione dell'ordine dei byte — 19 ottobre 2026

`DeviceErrors` e la coda payload non costituiscono una sequenza unica. Azzerare il decoder
prima di elaborare byte gia accodati poteva interpretare un vecchio Enter o prefisso nel
nuovo flusso. Decisione utente: scartare tutto l'input incerto e ripartire con riga vuota.

Il consumer controlla il guasto anche quando trova EMPTY. Scarta il valore gia estratto,
svuota la coda senza yield e azzera decoder, modificatori, riga e indicatore di troncamento.
Il contratto attuale e BSP con consumer IF=0: il produttore IRQ non puo riempire la coda
durante questa operazione. Il limite e 258 pop: 256 dati, un LOSS e EMPTY. Un risultato
DENIED/BUSY o l'assenza di EMPTY entro il limite resta terminale. Non cambia il contratto
del payload; la politica LOSS ordinata resta distinta dal guasto fuori banda.

`DevicePending` conserva la notifica anche dopo la saturazione di `DeviceErrors`.
La ripresa avviene alla successiva esecuzione del consumer; questa modifica non aggiunge
un evento hardware. Lo svuotamento riguarda la coda software, non garantisce che il
controller o il dispositivo non abbiano altri byte pendenti.

`runtime/consumer/stream.c` contiene il consumer reale, `recovery.c` la politica di recupero;
`runtime/task.c` conserva worker e dispatcher delle task.

## Fonti e limiti

Riferimento primario del dispositivo emulato: [QEMU v11.1.0, pckbd.c](https://github.com/qemu/qemu/blob/v11.1.0/hw/input/pckbd.c).
`KBD_STAT_*` definisce OBF al bit 0, AUX al bit 5, timeout al bit 6 e parita al bit 7;
`i8042_realizefn` registra dati a 0x60 e stato/comandi a 0x64.
`kbd_read_data` consuma dati solo con OBF e aggiorna la sorgente IRQ.

Questa fonte descrive QEMU, non certifica un controller fisico. Timeout/parita sono
esercitati con porte simulate; non sono guasti prodotti dal dispositivo QEMU nei test.
Il drain hardware resta limitato a 64 letture: non prova che il dispositivo sia vuoto.
L'eventuale migrazione del drain al worker richiederebbe un nuovo progetto di ownership,
notifiche e gestione del canale mouse condiviso, non la sola sostituzione di una funzione.

## Verifica della correzione

Il nuovo `consumer_test.c` collega il consumer C, decoder/testo Rust e editor C reali;
coda, scheduler, output e invio shell sono simulati. Copre lunghezze 0..256, LOSS pendente,
vecchi Enter/prefissi, riga troncata, reset dei modificatori, recupero anche su coda vuota,
nuovo comando dopo il recupero e DENIED/BUSY prima o durante lo svuotamento.
Il controllo runtime separato copre anche il limite delle 258 letture e la saturazione.

## Prove storiche del 18 ottobre

Suite host (12 su 12; test ps2 runtime 53695 e 53757 controlli con e senza mouse), QEMU tutti
exit 0: 10 boot mouse+puntatore+shell, stress da 200 tasti, 10 boot tastiera
(`kernel/build/b1-campaign/b2`).

## Evidenze del 19 ottobre

- Regressione riprodotta sul consumer di HEAD `da56030`: il vecchio Enter arriva
  all'invio shell dopo il reset (`kernel/build/b2-regression/result.json`, uscita attesa 86).
- Suite host: `kernel/build/b2-recovery-host-verified.log`, evidenze
  `kernel/build/ps2-decoder-2c6352880d45484b807cba144a3dd263`.
  Consumer: 262 scenari per variante seriale/video, MSVC /W4 /WX e ASan;
  runtime: 87.627 e 87.689 controlli senza/con mouse. Rust debug/release e Clippy passano.
- Analisi del nuovo banco consumer: `kernel/build/b2-consumer-cppcheck.xml`, nessun
  diagnostico bloccante; il runner ora lo include anche nella suite completa.
- QEMU: dieci boot consecutivi, directory
  `kernel/build/ps2-input-firmware-ef12b928799c45eba8bb573aa06d060b`;
  immagine SHA256 `B1ACB08C3D24DB129F5580D286D249F75C9AE7E162BD1A9C1E49E3EBA0920DF3`.
  Tastiera italiana, shell, mouse e pixel del puntatore; ogni budget fisico torna
  da 20.407 a 20.407 pagine, marker finale KERNEL_READY, nessun fault inatteso.
- Audit MAP: 2.125 simboli, probe vietati assenti, nessun oggetto test linkato.
- Struttura: 67 sorgenti del percorso input verificati, nessuna violazione.

ASan verifica i processi host, non certifica i leak di RAM fisica. Il ripristino del
budget QEMU e una prova separata. Valgrind non eseguito; coverage linee/branch non misurata.
