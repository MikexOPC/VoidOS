# Puntatore console — verifica del 12 ottobre 2026

Checkpoint precedente alla segnalazione manuale di lag e panic.
Per lo stato aggiornato vedere [correzione e input continuo](pointer-latency.md).

Implementato nel profilo `-Ps2Mouse -ConsoleShell`: bitmap originale24x24,
interno bianco, contorno nero e trasparenza; hotspot locale3,1 sulla punta.
Rust valida posizione/delta e clamp; C possiede stato, dirty e disegno.
Il [design approvato](pointer-design.md) conserva la griglia RAM come fonte
del ripristino e il worker esistente come unico produttore di pixel.
Per la prova nella finestra QEMU: [avvio interattivo e comandi](pointer-manual.md).

## Implementazione e review

- `pointer/state.c` possiede un owner statico originale associato alla griglia.
  Owner copiato, coordinate/delta invalidi, contatore esaurito e movimento
  dopo hide/close vengono respinti. Nessun heap o backup dei pixel.
- `pointer/rust/movement/plan.rs` vieta unsafe; il piccolo export in
  `pointer/rust/ffi/movement.rs` usa valori scalari C, senza puntatori Rust.
- Movimento marca al massimo12 celle vecchie e12 nuove. Le posizioni
  intermedie ancora dirty restano da ripristinare; nessuna invalidazione persa.
- Il framebuffer compone testo/caret e maschera nella stessa sessione
  di massimo8 celle, con verifiche complete delle risorse prima/dopo.
  I wrapper senza puntatore conservano il percorso precedente.
- Solo task3 aggiorna il puntatore; task1 disegna. Sessioni estranee,
  task errata, service Busy/Closed/Failed e fallimento del planner non
  possono pubblicare un aggiornamento. Nessun disegno dagli IRQ.
- Durante Quiet il worker nasconde il puntatore e completa le celle dirty;
  close richiede immagine nascosta e griglia senza lavoro pendente.
- Marker di frame completato riportano coordinate, visibilita, movimenti
  e celle renderizzate; non dichiarano un frame completo mentre restano dirty.

Nessun nuovo registro hardware, istruzione Assembly o mapping introdotto.
Restano il framebuffer RGB/BGR32 UC, le guardie, NX e i lease gia verificati.
La mutazione RAM e serializzata sul BSP a IF0; non prova sincronizzazione SMP.

## Immagine e prove

Immagine combinata: `kernel/build/pointer-final/kernel.elf`, SHA256
`2BC8FCBBC21FA480437F2D78A4A66AEEC6A522F017C476EC156986D0925AE4C9`.
Build `kernel/build/pointer-final-build.log`, warning come errori.
Loader invariato: SHA256
`2D87ABD8C7CAD8D0C2214259C0FA9479ED75D8A259AFFD28A9AF1738765C2A02`.
I manifest delle campagne registrano anche QEMU/OVMF e hash dell'ESP.

| Prova | Risultato | Evidenza in `kernel/build/` |
| --- | --- | --- |
| Console host C/ASan, Rust debug/release/Clippy | PASS;25768 controlli pointer,8833 worker per profilo;6010 wake/close tastiera e6040 mouse/adattatore | `pointer-host-complete/`, `pointer-host-complete.log` |
| Planner Rust | Due test in debug e release;576000 combinazioni valide per profilo, piu stati/delta invalidi | Log host sopra |
| Runner Python |19 regressioni console e5 nuovi casi pointer; geometria/padding, bitmap, cleanup e letture HMP fresche | Log host e invocazione finale `test/kernel/console/video/pointer/firmware_test.py` |
| Backend framebuffer C/ASan | PASS; overlay sopra testo/caret, RGB/BGR, padding, trasparenza, rimozione e input invalido;100 ripetizioni | `framebuffer-owned-12a8ff5dfeef41cdaaab77ff0ca1edb6/`, `pointer-framebuffer-complete.log` |
| Primo boot con scene live | PASS, cinque scene | `ps2-input-firmware-32353c8ca7a5477588c44c18aa9c5e0a/` |
| Campagna finale | PASS10/10 consecutivi sulla stessa immagine:98 tasti,10 comandi,51 eventi/153 byte mouse per boot; perdite0 | `ps2-input-firmware-1c71574491d84c9285d05dbada6ee408/` |
| Stress | PASS:812 tasti,778 scalar,12 righe/comandi, riga piena respinta;51 eventi mouse e cinque scene | `ps2-input-firmware-01248b92496e4159909799223fe43a3e/` |
| Video assente | PASS:98 tasti,10 comandi,35 eventi mouse; pointer e console video non inizializzati | `ps2-input-firmware-a418c4b2a7de46a3a3dbaea74a525d3c/` |
| Console senza mouse | PASS:98 tasti,10 comandi, pixel/caret e budget ripristinato | `ps2-input-firmware-00d5d677882a44978c2612a7ac8e2721/` |

Ogni boot pointer confronta cinque scene: posizione iniziale, clamp al bordo
inferiore destro e superiore sinistro, poi dopo status e echo exit. Per scena:
576 pixel fisici e786432 pixel visibili confrontati con testo/caret e maschera
attesi; il confronto dell'intera area trova anche residui delle vecchie posizioni.
Sono50 scene nella campagna consecutiva. Il cleanup verifica di nuovo l'intera
console senza puntatore, oltre ai marker IRQ/stop/collect e `KERNEL_READY`.

Dieci boot: budget20417 prima/dopo, MaxPm massimo79767, Overruns0.
Stress:68145 celle/9137 gruppi, MaxPm257275, Overruns0, budget20417 ripristinato.
Senza video: budget20462 prima/dopo. Queste sono misure del profilo QEMU,
non quote RAM o benchmark universali.
Totale13 boot finali positivi, oltre al primo boot di collaudo. Regressione
senza mouse: `kernel/build/pointer-keyboard-regression/kernel.elf`, SHA256
`C637A91F74A400D354BC0D2BB77B6BB601E03510BC042CFCAD38ABEBF5710B0E`.

La geometria del runner e stata poi rafforzata per verificare Used/Capacity
e accettare stride con padding. I dati della campagna iniziale/consecutiva
usano stride1024 e soddisfano anche questi controlli; stress e test Python
verificano la versione aggiornata. Nessuna modifica al codice eseguibile
dopo la compilazione dell'immagine finale.

## Diagnostica, artefatti e limiti

Cppcheck2.22 nei runner console/framebuffer: nessuna diagnostica bloccante
error/warning/performance/portability; informazioni su include di sistema
e funzioni isolate restano separate. Review di struttura e diff nel perimetro;
sorgenti entro400 righe e cartelle entro5 sorgenti. Lo script build principale
e400 righe; l'export Rust pointer e in un helper funzionale separato.

Il primo tentativo Rust vietava anche l'attributo di export `no_mangle`, quindi
la build si fermava. Corretto separando il planner puro con forbid unsafe
dall'export FFI; nessun boot di quella build viene contato. Nessun crash
inatteso nelle campagne del puntatore riportate sopra.

Il runner delle letture fisiche esclude i vecchi output HMP per ogni richiesta:
un test con due letture dello stesso indirizzo conferma dati nuovi. Evita
che una cache accidentale del log venga scambiata per una verifica del video.

Anteprima reale: `ps2-input-firmware-32353c8ca7a5477588c44c18aa9c5e0a/boot-1/pointer-screen.png`;
`pointer-detail.png` e il ritaglio ingrandito senza interpolazione. Non sono
mockup. I singoli snapshot PPM e i contatori JSON restano nelle campagne.

ASan riguarda C host; Rust/Assembly/kernel fisico non sono strumentati.
Valgrind non eseguito, coverage linee/branch N/D. Accounting della sessione
verificato in QEMU; nessuna certificazione generale di leak o hardware fisico.

Bitmap fissa, senza antialiasing, animazione o cursore hardware. Il punto
attivo e descritto, ma i pulsanti non producono ancora azioni GUI. La bitmap
resta interamente nell'area della griglia, escludendo margini firmware; un
futuro compositore dovra possedere lo sfondo dell'intero desktop. I gruppi
vengono disegnati progressivamente: nessuna promessa di frame atomico o vsync.
Le verifiche live riguardano scene completate. Nessun userspace, SMP, finestre
o nuovo gestore di layer introdotto in questo blocco.

Review: APPROVE per questo profilo QEMU/BSP; gate del blocco completati nei
limiti dichiarati. Il passo successivo richiede un contratto distinto per
superfici grafiche, composizione o azioni dei pulsanti.
