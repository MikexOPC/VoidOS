# Mouse PS/2 collegato al kernel — 11 ottobre 2026

Perimetro autorizzato: mouse, owner condiviso PS/2, adattatori scheduler,
registro/frame IRQ e watchdog. Quattro task e vettore42h, come scelto
dall'utente. Il controllo EOI del LAPIC è parte dell'adattatore IRQ:
accetta42h soltanto con il relativo bit ISR presente e tutti gli altri
ISR vuoti. Nessuna modifica alle istruzioni di ingresso Assembly.

Il profilo `-Ps2Mouse` richiede il dispositivo standard emulato da QEMU.
Due code256 byte, eventi4/6, consumer0/3 e subscription41h/42h.
Ogni handler sceglie la coda da status.AUX, non dal vettore entrato;
EOI conferma soltanto il vettore realmente in servizio. Nessuna decodifica
o stampa nell'handler. Il worker attende evento6; la tastiera può continuare
a svegliare il proprio consumer tramite IRQ ricevuti dal dispatcher.
Lo shutdown ferma entrambi i dispositivi, drena IRQ, revoca le subscription,
svuota le code e restituisce64 pagine di stack pinned. Errori mantengono
ownership e fail-stop. Non è un driver certificato su hardware fisico.

## Host e sicurezza

- Rust: sette casi in debug e release; Clippy con warning come errori.
- ABI C/Rust:2.097.152 pacchetti esaustivi, bounds, alias e reset.
- Trasporto:169.985 controlli;1.000 cicli precedenti e1.000 cicli pair
  start/stop; fallimenti di ogni callback nel percorso pair positivo.
  Include payloadFA residuo, reset e verificaFA/AA/00 sul canale AUX.
- Task mouse:53.000 controlli,5.000 cicli; decoder C/Rust reale, scheduler,
  payload e output mock. Pacchetti parziali, LOSS, header invalido e overflow.
- Backend PS/2:4.744 controlli tastiera e4.814 col profilo mouse, MADT reale
  e I/O mock; route12/ISO/alias, selettore ripristinato e canali incrociati.
- Registro IRQ: tre subscription simultanee, token stantii e ID duplicati;
  due casi Rust. Frame: due casi Rust con42h e IF verificato.
- Accounting:420.002 controlli per esecuzione della fixture a2/3/4 lease;
  identità, pin, overlap, budget e rollback. Suite scheduler completa passa.
- LAPIC/APIC: suite host con42h, vettore errato, ISR multipli e mancato
  azzeramento dopo EOI. Registri hardware sono mock in queste prove.
- MSVC /W4 /WX e ASan C passano; cppcheck senza error/warning/performance/
  portability nei sorgenti mouse e negli adattatori. Style/information
  restano nei report XML. Coverage linee/branch N/D; Valgrind non eseguito.
  Il percorso mouse non alloca heap; ASan non prova il rilascio RAM fisica.

Evidenze host: `kernel/build/mouse-runtime-host-reviewed.log`,
`kernel/build/ps2-mouse-7d86e0b16f9f45808b667ae8c8d8191a/`,
`kernel/build/mouse-runtime-ps2.log`,
`kernel/build/ps2-decoder-977638367655489887dfa09df876af28/`,
`kernel/build/mouse-runtime-registry/`, `kernel/build/mouse-runtime-apic/`,
`kernel/build/scheduler-context-8a1ccc496ed14ab5af40473eb9fbf187/`,
`kernel/build/mouse-runtime-adapter-static/cppcheck.xml`.

## Firmware e immagini

Immagine mouse corrente: `kernel/build/mouse-runtime-current/kernel.elf`,
SHA256 `6360FC6FDBFA1259722150460AF89864884B07BDF532C448F8AE884B885ED77F`.
Un boot mouse più shell seriale italiana passa:
`kernel/build/ps2-input-firmware-4c3d2bbbf3684bd38cc16131609cacc7/`.
Dieci boot consecutivi della stessa immagine passano:
`kernel/build/ps2-input-firmware-0c1326ec3c284c3fbe24f52b99a80b57/`
e `kernel/build/mouse-runtime-reviewed-boot.log`. Per boot:35 eventi/105 byte
mouse verificati,16 tasti, IRQ12 durante il worker, LOSS0 e budget20.460
prima/dopo. Il boot con shell verifica anche98 tasti e10 comandi.

Due campagne precedenti da dieci boot ciascuna sono prove aggiuntive:
`kernel/build/ps2-input-firmware-69ecfd344ca2467995800aca12e43433/` e
`kernel/build/ps2-input-firmware-48b972b3f46c4f9e94ead594c7a4dc46/`.
Quest'ultima confronta35 eventi/105 byte mouse per boot,16 tasti,
tre pulsanti e movimenti durante la pressione dei tasti; IRQ12 nel worker,
LOSS0 e budget20.460 ripristinato. Hash precedente:
`DD2F4631672A53254AE2898060915E0ADC4E277C3701642A04D48279C7F659FD`.
Non sostituiscono la verifica dell'immagine ricompilata corrente.

Ordinario: dieci boot con CPU/memoria verificati, evidenza
`kernel/build/scheduler-firmware-9374eca0838e4fcc88db102d46a873e4/`;
immagine `mouse-runtime-ordinary/kernel.elf`, SHA256
`A48AD4BD4FA6FE289974A8736169B4EF52540176CBE660BA0A71D41D434ECCAD`.
Console video con diagnostica al punto di flush: dieci boot consecutivi
eseguiti senza altre VM passano, pixel/cursore e budget verificati:
`kernel/build/ps2-input-firmware-1820dfc194e1406fae2e6802f38cfeae/`;
SHA256 `3BB7C2A345A4A79D7523910458796504E267516D7229A89B9798E9352D3F3A62`.

## Fallimenti conservati e gate aperto

Il primo test di movimento si arrestava conISR66 ancora presente:
EOI ammetteva solo40h/41h. Correzione verificata sul mock LAPIC e in QEMU;
evidenza fallita `kernel/build/ps2-input-firmware-2f0cc449ba0540a487e1831de2a80d5a/`.
I primi tentativi host APIC mancavano dipendenze del runner bootstrap;
il test finale usa i validatori appropriati, senza cambiare il runner.

Una campagna console eseguita insieme ad altre VM passa cinque boot e
si ferma nel sesto: `kernel/build/ps2-input-firmware-b8394dd442e64bc19898674c2b1ff027/`.
MarkerCPU `5343484641494c01`, worker IF0, nessun triple fault eISR vuoto.
Il carico concorrente e il limite temporale del renderer sono ipotesi,
non una causa dimostrata. Aggiunta misura PM al fallimento di flush nel
chiamante PS/2; il limite del renderer non è stato alzato.
Dieci boot successivi passano, ma non cancellano questo episodio.
Estensione circoscritta alla diagnostica console autorizzata dall'utente
secondo direttive-locali §1. Diagnostica e nuove campagne nel
[report console](../../../../../console/video/documentation/worker-diagnostics.md).
Questo era il gate aperto del checkpoint mouse. Il successivo blocco console
ha riprodotto e corretto il fail-stop temporale, con scelta dell'utente e
prove prima/dopo: [correzione](../../../../../console/video/documentation/latency-fix.md).
Il vecchio dump resta insufficiente per attribuire con certezza quell'episodio.

## Riproduzione e limiti

Build: `kernel/scripts/build.ps1 -Ps2Mouse -OutputDirectory <directory>`.
Firmware: `arch/x86/kernel/drivers/input/ps2/scripts/firmware.py
--image <kernel.elf> --boots 10 --mouse --stress 10`.
`--text --shell` verifica anche la shell seriale; il profilo mouse
esclude i profili di console video. Movimento e pulsanti sono eventi
seriali: nessun puntatore grafico, rotella o compositore introdotto.
BSP/cooperativo, IRQ nelle finestre Assembly verificate e nel dispatcher;
non preemption, SMP, userspace o prova su PC fisico.

Fonti e contratto: [runtime-design](runtime-design.md),
[QEMU pckbd.c v10.1.0](https://github.com/qemu/qemu/blob/v10.1.0/hw/input/pckbd.c),
[QEMU ps2.c v10.1.0](https://github.com/qemu/qemu/blob/v10.1.0/hw/input/ps2.c),
[Intel SDM Vol3 §10.8.5](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html).
Il binario QEMU installato è11.1.0; identità loader/OVMF/QEMU e immagini
sono registrate nei manifest delle campagne, non equiparate al sorgente10.1.0.
