# Puntatore: lag, perdita input e regressioni — 13 ottobre 2026

## Segnalazione e riproduzione

La prova manuale ha mostrato lag e scomparsa del cursore. Il log della sessione
`kernel/build/pointer-interactive-3a4015558eca4733b0a29c424c479819/serial.log`
termina con `MOUSE_LOSSES=1` e `PS2_INPUT_FAILED`: la chiusura delle task
nasconde deliberatamente il puntatore. Non era una prova di stabilita passata.
I movimenti distanziati dei test precedenti non coprivano questo carico.

Un nuovo test con movimenti continui ha riprodotto anche
`SCHEDULER_DISPATCH_FAILED`, watchdog failure14/detail1:
`kernel/build/ps2-input-firmware-0223a10dba6649e494cb90ea23d6214b/boot-1/serial.log`.
Il budget dei risvegli HLT era8, insufficiente durante input frequente prima
della scadenza HPET. Il risultato non autorizzava a dichiarare guasto il timer.

## Correzioni e contratto concordato

- La task mouse decodifica un pacchetto di tre byte prima del yield. Un
  pacchetto incompleto resta nello stato Rust; la task attende se la coda e
  vuota. Evento, errore o perdita continuano a cedere la CPU. Nessuna
  aggregazione degli eventi, modifica dei pulsanti o cancellazione delle perdite.
- Nel profilo console con mouse, la finestra worker passa da100ms a1ms,
  anche quando la griglia non e dirty. Gli altri profili conservano il loro
  limite precedente. Il rendering conserva gruppi di massimo8 celle e
  verifica completa del paging per gruppo.
- La scelta utente porta soltanto `x86_watchdog_idle` a5ms; `wait` resta
  a100ms. Ticks ricavati dalla frequenza HPET con la politica Rust esistente,
  prima di arm e soltanto con owner non armed. Nessun ritorno anticipato
  prima dell'acknowledgement del watchdog.
- Il budget HLT e256 risvegli, sotto il massimo1024 dell'API Assembly.
  La scadenza temporale HPET resta distinta dal limite finito anti-storm.
  Il backend restituisce risvegli+1:257 e valido,258 e zero sono respinti.
  Count, Error, Armed, mapping, token e LAPIC restano verificati.
- Il profilo input e il gate del dispatcher ammettono32768 idle finiti,
  evitando di ridurre accidentalmente la prova a circa5 secondi. Restano
 65536 dispatch massimi: non e una sessione desktop permanente.
- La bitmap24x24 ha sagoma piu piena e contorno nero piu regolare, con
  margine laterale di due pixel e controllo delle diagonali. Trasparenza,
  interno bianco, hotspot e contratto di ripristino dalla RAM invariati.

HLT puo risvegliarsi per interrupt prima del timer desiderato:
[Intel SDM Vol2A, istruzione HLT](https://cdrdv2-public.intel.com/812383/253666-sdm-vol-2a.pdf).
Conversione del periodo in femtosecondi: [HPET1.0a §2.3.4](https://www.intel.com/content/dam/www/public/us/en/documents/technical-specifications/software-developers-hpet-spec-1-0a.pdf).
Il backend esistente conserva counter0F0h (§2.3.7), interrupt enable bit2
del timer (§2.3.8), comparator108h+20h*N (§2.3.9) e ack W1C020h (§2.3.6).
La modifica non introduce nuovi accessi a registri o bit.

## Test e diagnostica

La regressione C sul consumo fallisce sul codice precedente:
`kernel/build/pointer-lag-before.log`. Dopo il fix,6000 lifecycle includono
1000 raffiche di85 pacchetti:654000 check, C/Rust reale con scheduler,
IRQ, payload e seriale mock. ASan, Rust debug/release e cppcheck passano:
`kernel/build/pointer-lag-mouse.log`.

Console:25768 check pointer,8833 check worker per profilo e test di
sessione, stato, rendering, ownership e cleanup. Evidenza
`kernel/build/pointer-lag-host.log`. Pixel RGB/BGR, contorno e ripristino:
`kernel/build/pointer-lag-framebuffer.log`.

Watchdog: ripetizioni dei nuovi casi17/257 risvegli positivi,258/zero
negativi; idle5ms e wait100ms verificati esplicitamente. Evidenza
`kernel/build/pointer-lag-watchdog-final.log`. Dispatcher: limite32768
accettato,32769 respinto, failure e raccolta invariati; evidenza
`kernel/build/pointer-lag-scheduler.log`.

Un ulteriore errore del test fisico leggeva risposte precedenti del monitor
QEMU, contenenti il cursore. La lettura framebuffer ora accetta soltanto
bytes scritti dopo la nuova richiesta. Due query consecutive sullo stesso
indirizzo e valori differenti sono verificate per entrambi i reader:
`kernel/build/pointer-lag-python.log`,5 casi Python iniziali;
la regressione finale aggiunge i controlli fallback:
`kernel/build/pointer-lag-python-final.log`,6 casi Python.

## Immagine e campagna finale

Immagine: `kernel/build/pointer-lag-final/kernel.elf`.
SHA256: `85A37473029B654E0FC746BCB4495ED2CFD4AE72C02951BBC5C2CA09CA2D91E2`.
Build warning come errori: `kernel/build/pointer-lag-build.log`.
La campagna del12 ottobre e stata interrotta dopo tre boot riusciti;
non viene contata come dieci boot consecutivi.

Campagna ripresa: `kernel/build/pointer-lag-qemu-resumed.log`.
Dieci boot consecutivi completati:
`kernel/build/ps2-input-firmware-75505195e3cd43d6bc142735ec1ccf35/results.json`.
Per boot:98 tasti,10 comandi,201 eventi mouse/603 byte, zero perdite,
6 scene live confrontate con screenshot e576 pixel fisici ciascuna.
Budget fisico20417 prima/dopo. Ogni boot invia150 movimenti continui
senza aspettare ogni evento; somma delta, ordine, posizione finale e
ripristino dello sfondo verificati. Totale1500 movimenti e60 scene.

Gli invii durano1,578..2,359 secondi; il drain dopo l'ultimo invio dura
0..47ms. Questo misura la coda del test, non la latenza di ogni movimento
o un framerate. Un gruppo nel boot3 supera100ms:736854 tick PM, circa205,85ms,
con un WARN e accounting coerente. Gli altri boot non segnalano overruns.
Il costo delle scansioni paging resta e non si promette fluidita costante.

Stress con input continuo e scroll:612 tasti,12 righe/comandi, un comando
troncato respinto,201 eventi mouse, zero perdite,6 scene verificate,
budget20417 ripristinato e nessun overrun. Evidenze:
`kernel/build/pointer-lag-qemu-stress.log` e
`kernel/build/ps2-input-firmware-1941f553dfdb4310a1de0113a9d8003a/results.json`.

Fallback senza video:98 tasti,10 comandi,35 eventi mouse, zero perdite e
budget20462 ripristinato. Evidenze: `kernel/build/pointer-lag-qemu-no-video-final.log`
e `kernel/build/ps2-input-firmware-ff87104d314a4b7ca207b7b61b1e7c91/results.json`.
Il primo test falliva sul criterio task IRQ: in questo profilo gli IRQ
erano arrivati durante HLT, non nelle task. Il criterio aggiornato non
richiede quella fase casuale nel solo fallback; conserva sequenza/count,
IRQ hardware0x41/0x42 nella traccia, zero perdite e cleanup. Nei profili
video e nel verificatore mouse ordinario, task IRQ resta obbligatorio.
Il nuovo caso host respinge comunque assenza di IRQ hardware, perdita,
accounting sbagliato, sequenza errata e cleanup mancante.

Launcher verificato in PowerShell5 e nel runner corrente:
`kernel/build/pointer-lag-launcher.log`,
`kernel/build/pointer-lag-launcher-powershell5.log`.
Smoke dell'ESP realmente preparata, con display none al posto di SDL:
`kernel/build/pointer-lag-launcher-smoke.log`; movimento17,9, help, Esc,
KERNEL_READY e cleanup pointer/console/PS2/mouse/scheduler verificati.
Questo non certifica l'interazione manuale con la finestra grafica.

Regressione console con sola tastiera:98 tasti,10 comandi, pixel/caret e
cleanup passati. Immagine `kernel/build/pointer-lag-keyboard/kernel.elf`,
SHA256 `544C11BADF1B3BB3F86AE8E5F575BC2AA063A44CB7B0C0BC3E103B8300E2B0C1`.
Evidenze `kernel/build/pointer-lag-keyboard-build.log`,
`kernel/build/pointer-lag-keyboard-qemu.log` e
`kernel/build/ps2-input-firmware-9fea5ef2fd93499eada037ec7b74c546/results.json`.

Revisione finale: `kernel/build/console-render-review.json` senza errori,
massimo398 righe sorgente; adattatori aggiornati verificati anche in
`kernel/build/pointer-lag-scope-review.json`. Diff del perimetro senza
errori di whitespace. Il codice eseguibile dell'immagine principale
non e cambiato dopo la campagna di dieci boot. Le successive modifiche
del verificatore riguardano soltanto il criterio di fase IRQ nel fallback;
il profilo positivo con video conserva il requisito task IRQ e i suoi gate.

## Limiti

La perdita resta terminale: non e implementata una recovery che scarti
silenziosamente input. Nessun heap nuovo; accounting fisico e cleanup
verificati in QEMU separatamente da ASan host. Valgrind non eseguito;
coverage linee/branch non misurata. Nessuna prova su hardware fisico o
automazione del movimento umano nella finestra SDL. La freccia e ancora
una bitmap senza antialiasing; non e un cursore hardware GPU.

Comando e limiti della sessione: [prova manuale](pointer-manual.md).
