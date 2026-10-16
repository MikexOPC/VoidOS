# Correzione del fail-stop temporale — 11 ottobre 2026

## Causa riprodotta e decisione

Il misuratore classificava ogni flush oltre357955 tick PM (circa100ms)
come errore terminale, anche quando il renderer aveva gia disegnato,
verificato e confermato il gruppo. Il servizio veniva marcato Failed e
il worker chiamava fail-stop. La soglia post-operazione mescolava un
problema di latenza con un errore del disegno o della memoria.

Riproduzione senza modificare kernel o clock: avvio normale della VM
posseduta, poi comando HMP `one-insn-per-tb on` dopo il marker del worker.
[QEMU documenta](https://www.qemu.org/docs/master/system/monitor.html)
che questa modalita TCG rallenta l'emulazione; non e una misura delle
prestazioni CPU reali. Dopo il primo input il test ripristina la modalita
normale. Il runner originale conserva deadline e cleanup della propria VM.

Vecchia immagine SHA256
`D46ED41BB82F846E77CBCAF8D94E8D686CFF9DEBA648C590185ECB8073A0A1F3`:
`kernel/build/ps2-input-firmware-b822a7c2fb944dd2b0e4465ba032d840/`.
FailurePM `0x7c408`, Flags `0x1000001`, MeasuredCells8 contro Rendered16;
worker stage2, Error0 e Losses0. Batch.Failed0 e Render.Busy0 distinguono
un gruppo completato dal fallimento della misura. Il test prima del fix
verifica questo rifiuto come negativo atteso, non come boot positivo.
Una prima riproduzione equivalente e conservata in
`kernel/build/ps2-input-firmware-79ba20daca9349d6b200dd2592349dbb/`.

L'utente ha scelto: WARN per ritardi validi nella console operativa,
diagnostica rigorosa. Non cambiare soglia o rimuovere verifiche del renderer.

## Correzione

API diagnostiche intatte: stato temporale3 resta terminale. La console
operativa usa `kernel_console_measure_batch_step_live`: solo sample valido
e non nullo, draw riuscito e accounting1..8 celle autorizzano l'overrun.
Contatori Overruns/SlowCells e LastTicks/MaxTicks restano verificabili;
Overruns non puo superare Cells. Nessuna nuova allocazione, cache di
autorizzazioni, retry o rollback pixel. Errori di clock, callback, ownership,
overflow e contabilita restano terminali. WARN limitati ai conteggi1,2,4,8...
evitano una stampa per ogni gruppo lento. Il worker conserva poll/yield
tra gruppi, con gli stessi confini IRQ e gli stessi limiti di lavoro.

Il controllo firmware accetta MaxPm oltre soglia solo se Overruns e
coerente, limitato al numero di gruppi, e il WARN e presente. Pixel fisici,
screenshot, transcript, caret, input, memoria e cleanup restano obbligatori.
Il manifest include ora Overruns nel risultato console.

## Prove host

Suite: `kernel/console/video/scripts/test.ps1`; include ora anche i tre
runner Python del modulo. Corrette fixture stale del valore restituito dal
transcript con caret e del marker RAM del servizio; aggiunti negativi sul
marker mancante e accounting overrun incoerente. Nessun controllo del
provider attenuato per rendere verdi queste fixture.

Il misuratore C/ASan prova strict/live, clock fermo, mask/sample invalido,
draw fallito e owner corrotto. Il percorso strict continua a rifiutare
limit+1; quello live contabilizza2 celle/1 gruppo lento, senza ripetere il
disegno quando la griglia e pulita. Rust mantiene la classificazione
numerica originale. Leak detector Valgrind e coverage linee/branch N/D;
ASan non certifica leak di RAM fisica.
Evidenze finali: `kernel/build/console-overrun-host-final/` e relativo `.log`;
19 test Python, MSVC warning come errori, ASan, Rust debug/release,
Clippy e cppcheck passano. Alcuni validatori CPU/PTE/HPET sono mock,
distinti dalle prove firmware e dai contatori di memoria reali.

## Regressione QEMU prima/dopo

Immagine corretta SHA256
`623F2F27E9D5094F8BE8100704D3674B6AB72B663F37CC147CCF2B957451D86B`:
`kernel/build/console-overrun-fixed/kernel.elf`.
Lo stesso rallentamento passa:
`kernel/build/ps2-input-firmware-5ea5d7cc3760485ca1206c8224b54d7c/`.
MaxPm486186, Overruns1, WARN,98 tasti,10 comandi,7.165 celle/1.009 gruppi,
pixel fisici e screenshot, cleanup, KERNEL_READY e budget ripristinato.

Un tentativo precedente raggiungeva KERNEL_READY, ma il vecchio harness
respingeva MaxPm oltre100ms: resta una prova fallita, non un PASS,
in `kernel/build/ps2-input-firmware-5120993cbd8d445d9e4163097c58ccd3/`.
L'esperimento iniziale con rallentamento fin dall'avvio non raggiunge il
worker entro40s: `kernel/build/ps2-input-firmware-7d6aeafd75fe423386f1aedfedac4a3b/`.
Non e evidenza della causa del fail-stop e non viene contato positivo.

Dieci boot consecutivi della stessa immagine corretta passano:
`kernel/build/ps2-input-firmware-758b09be1d57492cbfd69aed6de251ae/`.
98 tasti e10 comandi per boot, pixel fisici e screenshot, zero overrun
osservati in questa campagna, MaxPm187771 e budget20.419 prima/dopo.
Nessun sorgente eseguibile modificato dopo la build di questa immagine.
Passa anche il boot lungo finale della stessa immagine:
`kernel/build/ps2-input-firmware-c6a9ef91eb594807b1aa2ac32c95ac0c/`.
812 tasti,2.460 byte,12 comandi, un overflow di riga gestito,
60.731 celle/8.149 gruppi, MaxPm288608 e Overruns0; pixel, caret,
KERNEL_READY e budget20.419 prima/dopo verificati. La riproduzione con
overrun e la prova lunga restano scenari distinti.
Review finale:103 file esaminati nel perimetro console, massimo sorgenti
398 righe, nessun errore di layout/link; PS/2:64 sorgenti verificati.
`git diff --check` passa. Nessuna modifica a `reuse/`.

Riproduzione automatica:

```text
python -B kernel/console/video/scripts/latency/firmware.py --image <immagine-prima> --expect-reject
python -B kernel/console/video/scripts/latency/firmware.py --image <immagine-corretta>
```

## Limiti

Il fail-stop temporale e riprodotto e corretto. Il dump dell'episodio
originale concorrente non conteneva lo stato del misuratore: resta
compatibile con questa causa, senza poterlo attribuire retroattivamente
con certezza. Il [report precedente](worker-diagnostics.md) conserva le
prove storiche. Non e un fix di ogni possibile fail-stop del kernel.
Il disegno rimane IF0, il clock modulo non rileva intervalli multipli del
wrap, e un gruppo lento puo ritardare gli IRQ. Nessuna garanzia hard realtime,
SMP, preemption o hardware fisico; ottimizzazione e fault hardware restano
lavori distinti.
