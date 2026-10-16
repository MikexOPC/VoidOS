# Sottolineatura console e chiusura PS/2 — 11 ottobre 2026

Modulo: console video e sole interfacce framebuffer necessarie. Estensione
PS/2 autorizzata esplicitamente dall'utente dopo la riproduzione del panic.
[Contratto approvato](caret-design.md). `reuse/` e licenza root intatti.

Stato: **APPROVE per il profilo QEMU v0**. Suite host PASS e22 boot finali:
10 video consecutivi, un fallback, uno stress e10 ordinari consecutivi.

## Implementazione e review

Rust restituisce l'indice validato della cella, incluso wrap differito.
C conserva Caret separato dai caratteri e marca la vecchia e nuova cella;
il servizio rifiuta metadati corrotti prima di modificare il transcript.
Il batch verifica che posizione, scalari e owner non cambino durante il
disegno. Il renderer singolo precedente rifiuta il cursore abilitato.

Il gateway compone le ultime due righe del bitmap 8x16, senza ulteriori
scritture rispetto ai128 pixel della cella. Le API precedenti sono wrapper
senza overlay. Indice fuori gruppo e font8x8 con overlay vengono rifiutati
prima delle scritture. Inventario, lease pinned, cache UC, pagine RW/NX e
limite di8 celle restano invariati. Nessuna nuova allocazione o handler.

Prove: testo invariato, dirty vecchio/nuovo, cancellazione, fine riga,
scroll, risposte intercalate, reentry, corruzione del metadato, mutazione
durante callback, RGB/BGR, glifi ripetuti, padding e rimozione dell'overlay.

## Panic isolato e regressione prima/dopo

La prima immagine si ferma su `SCHEDULER_DISPATCH_FAILURE=6` alla chiusura:
`kernel/build/ps2-input-firmware-1f4eef4a21db479c95c1bcc8d3b4414c/boot-1/`.
Una ripetizione passa: il difetto è intermittente, non un PASS affidabile.
Sonde temporanee soltanto negli errori localizzano il rifiuto in
`kernel_ps2_stop`: campagna debug interrotta al quarto boot,
`kernel/build/ps2-input-firmware-ccd1f606ad964bab81c44948969c502d/boot-4/`.
Le sonde sono rimosse dalla working tree e dalle immagini finali.

Il mock riproduce deterministicamente un byte tastiera pendente prima
del readback mode o dell'ACK F5. La regressione fallisce prima del fix,
`kernel/build/caret-ps2-red.log`, controller assertion riga267 nel test
di quel tentativo. La traccia QEMU individua la fase stop, non distingue
il singolo accesso di porta: non viene inventata una traccia per-byte.

Fix: scrittura IRQ-off senza readback durante scanning attivo; stop F5
con massimo32 byte residui totali, tre invii su RESEND, solo FA come ACK;
poi close verifica il mode con interfacce ferme. Gli altri comandi restano
rigorosi. Mancato ACK, aux, parity, callback fallita, clock fermo e33 byte
residui falliscono mantenendo DIRTY. Il readback finale resta obbligatorio.

Fonti hardware verificate: [QEMU v10.1.0 pckbd.c](https://raw.githubusercontent.com/qemu/qemu/v10.1.0/hw/input/pckbd.c),
`kbd_safe_update_irq`, READ_MODE/WRITE_MODE e `kbd_write_data`;
[ps2.c](https://raw.githubusercontent.com/qemu/qemu/v10.1.0/hw/input/ps2.c),
`ps2_write_keyboard`, RESET_DISABLE F5 e risposte FA/FE.
Porte60/64, OBF bit0, IRQ1 mode bit0: nessun nuovo registro o bit riservato.
Versione installata QEMU11.1.0, round trip verificato; non è prova su8042 fisico.

## Host e analisi statica finali

- `kernel/build/caret-host-reviewed.log`: console374635 check; servizio60207;
  batch2048 workload sparsi, Rust cursor5/text2/timing4 casi in debug e release,
  gateway2228226 casi scalari, snapshot live e inventario. C con ASan;
  MSVC `/W4 /WX`, Rust warning come errori e Clippy; cppcheck senza diagnostiche
  error/warning/performance/portability bloccanti.
- `kernel/build/caret-ps2-final.log`: controller509433 check,10000 lifecycle,
  ogni callback fallita, tail0..33, RESEND/ACK assente/aux/parity/clock fermo;
  decoder, input italiano, editor, shell, route e rollback esistenti passano.
  Directory `kernel/build/ps2-decoder-0723a71976314259963c7223ff87a720/`.
- `kernel/build/caret-framebuffer-reviewed.log`:7 gruppi lifecycle,3 pixel,
  5 drawing ripetuti100 volte, compresi overlay e ripristino RGB/BGR.
  ASan C e accounting del pool mock, con budget128 restituito.
- `kernel/build/caret-review.json`: max356 righe, max5 sorgenti per cartella
  funzionale modificata. Layout centralizzato e `git diff --check` passano.

ASan non strumenta la VM, i moduli Rust separati o Assembly. Nessun heap
nuovo; Valgrind non eseguito sulla toolchain Windows. Coverage linee/branch
N/D. L'accounting fisico QEMU è distinto dai pool simulati host.

## Immagini e firmware finali

Shell `kernel/build/caret-reviewed-image/kernel.elf`, SHA256
`455B49FE70CB1AC153E617EF875ED77D26BF698BFCFD84F6F54BFA030041C45A`.
Ordinario `kernel/build/caret-ordinary-image/kernel.elf`, SHA256
`AADF9E1A4E1FDFD98F2899A1CDCD04487791032C07D1B5DF592BBADCC543A4DA`.
Loader BootKernel SHA256
`2D87ABD8C7CAD8D0C2214259C0FA9479ED75D8A259AFFD28A9AF1738765C2A02`;
identità QEMU/OVMF e immagini riportate nei rispettivi results.json.

- Dieci video consecutivi: `kernel/build/ps2-input-firmware-97f2c45bc88a4d58afbd0c33babe61f7/`.
  Budget20419 prima/dopo ogni sessione;71236 celle,9977 gruppi;
  768 pixel fisici e786432 pixel visibili verificati per boot, inclusa
  sottolineatura. Massimo106129 tick PM, circa29,65ms per flush: gate100ms
  passa; obiettivo10ms non dimostrato, nessuna promessa real-time.
- Un fallback seriale senza video: `kernel/build/ps2-input-firmware-210cc0a0d4af43d1ac1f7b80530d6490/`.
- Una sessione stress,200 pressioni aggiuntive, riga piena e idle-race:
  `kernel/build/ps2-input-firmware-e37532ee0261442193ee8d03662ebcc0/`.
  812 tasti,811 release,12 comandi, un rifiuto di riga piena previsto,
  budget20419 ripristinato e confronto pixel completo PASS.
- Dieci ordinari consecutivi: `kernel/build/framebuffer-live-234725e9d8da45c295a27caaf5421f4d/`;
  `results.json` e `console-grid-results.json`, avvio e identità immagine verificati.
- Dieci avvii precedenti del fix PS/2, prima dell'ultima validazione dei
  metadati servizio: `kernel/build/ps2-input-firmware-5b96d2a595d440a4bf5fae3a66146eb2/`.
  Sono prove aggiuntive storiche, non sostituiscono la campagna finale.

Riproduzione: script `kernel/console/video/scripts/test.ps1`,
`kernel/drivers/input/ps2/scripts/test.ps1`, framebuffer `scripts/test.ps1`;
build `kernel/scripts/build.ps1 -ConsoleShell -OutputDirectory <directory>`;
`arch/x86/kernel/drivers/input/ps2/scripts/firmware.py --image <kernel.elf>
--boots 10 --text --shell --console-shell`. Fallback aggiunge `--video-absent`;
stress usa `--boots 1 --stress 200 --line-stress --idle-race`.

## Limiti

Profilo BSP cooperativo con task kernel fidate; mutazioni e MMIO C a IF=0,
IRQ nelle finestre Assembly esistenti. Cursore fisso, nessun blinking,
mouse, compositore GUI o userspace introdotto. L'ultima immagine resta
visibile dopo close; nessun rendering successivo alla revoca del servizio.
Non sono dimostrati hardware fisico, SMP o isolamento di processi.
