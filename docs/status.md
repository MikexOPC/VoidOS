# Stato corrente — 14 ottobre 2026

Questo e il riferimento breve dello stato reale. I checkpoint in
`docs/review/` e i report di modulo restano evidenza del blocco in cui
sono stati scritti; se una frase al presente li contraddice, vale questo
file.

I test sono centralizzati in `test/kernel/`, `test/arch/x86/` e `test/lib/`.
Il runner `scripts/test/run.ps1` richiama le suite originali in processi
separati; i gate QEMU restano distinti dai test host. Vedere
[organizzazione ed esecuzione](../test/README.md).

Checkpoint 16 ottobre: console con cella 10x20 (102x38 a 1024x768) e puntatore 18x18
nitido nero con contorno bianco, da immagine di riferimento. Mouse PS/2 tollerante a
perdite, header errati e overflow; scansione dirty a byte e contatori di tempo dei flush.
10 boot mouse+puntatore+shell, stress, senza video, diagnostici framebuffer/console e
ordinario passano. La fluidita non e certificata e il limite del display QEMU e solo
un'ipotesi: [cella](../kernel/console/video/documentation/font-cell.md),
[puntatore](../kernel/console/video/documentation/pointer-sharp.md),
[costo dei flush](../kernel/console/video/documentation/flush-cost.md),
[mouse](../kernel/drivers/input/ps2/mouse/documentation/resync.md).

Blocco corrente completato nei test: transazione save/restore del cursore approvata,
backup fisso24x24, verifica completa e tre rettangoli autorizzati.
Movimenti attivano una sola cella; il vecchio sfondo viene ripristinato
dal backup e il nuovo salvato dopo il testo. Pacchetti senza spostamento
non richiedono ridisegno. Revoca e transazioni annidate sono negate mentre
il mapping e occupato; dopo scritture fallite resta trattenuto.
Passano dieci boot consecutivi,6000 invii continui e60 scene, zero perdite
e zero gruppi oltre100ms; passano scroll e fallback, con budget ripristinato.
Fluidita manuale e60FPS non certificati; nessun nuovo driver GPU.
[Contratto](../kernel/console/video/documentation/cursor-transaction-design.md)
e [prove](../kernel/console/video/documentation/cursor-transaction-validation.md).

Checkpoint precedente: una seconda prova manuale ha riprodotto
perdita e scomparsa dopo557 eventi. Il precedente test non copriva
adeguatamente il carico. Nuovo consumo a raffiche di32 pacchetti,
priorita alla posizione corrente e freccia arrotondata antialias concordati.
Passano10 boot consecutivi con6000 invii continui, zero perdite e60 scene
confrontate; passano stress con scroll e fallback. Le scansioni paging
restano costose: due boot segnalano un gruppo lento. La fluidita nella
finestra SDL non e certificata dai test automatici.
[Implementazione e nuove prove](../kernel/console/video/documentation/pointer-burst.md).

Checkpoint precedente: la prova manuale del puntatore ha prodotto
perdita mouse e panic, non coperti dai movimenti distanziati del checkpoint.
Corretti consumo per pacchetto, polling1ms, idle HPET5ms e budget dei risvegli;
sagoma con contorno piu regolare. Dieci boot consecutivi finali passano,
con1500 movimenti continui,60 scene controllate, zero perdite e budget
ripristinato. Passano stress con scroll, fallback senza video e smoke
dell'ESP interattiva e regressione della console con sola tastiera.
Un gruppo ha superato100ms ed emesso il WARN previsto:
resta un costo del paging e non e dimostrato un framerate costante.
[Cause, prove e limiti](../kernel/console/video/documentation/pointer-latency.md).

Checkpoint precedente: puntatore bianco a contorno nero24x24 nel profilo
`-Ps2Mouse -ConsoleShell`. Rust valida movimento e clamp; un solo worker
compone testo/caret e maschera, ricostruendo lo sfondo dalla griglia RAM.
Dieci boot consecutivi passano con50 scene live confrontate tramite screenshot
e pixel fisici,51 eventi mouse per boot, nessuna perdita e budget ripristinato.
Passano anche stress da812 tasti con scroll, fallback senza video e console
senza mouse:13 boot finali positivi, oltre al primo collaudo.
C/ASan, Rust debug/release/Clippy, test Python e cppcheck passati. Bitmap fissa
nell'area della console; nessuna azione GUI dei pulsanti, finestre o compositore.
[Design](../kernel/console/video/documentation/pointer-design.md) e
[prove, hash e limiti](../kernel/console/video/documentation/pointer-validation.md).
Disponibile l'[avvio interattivo](../kernel/console/video/documentation/pointer-manual.md)
per provare il cursore nella finestra QEMU; ESP isolata, log seriale e opzione
di ricompilazione. Preparazione e smoke input/cleanup verificati; nessuna
sessione desktop permanente o prova manuale umana automatizzata dichiarata.

Checkpoint completato: integrazione `-Ps2Mouse -ConsoleShell`, quattro
task e un solo worker video. La prima campagna passa otto boot e fallisce
nel nono nel watchdog idle. Corretto il rifiuto di IRQ input pendenti con
token live; regressione host prima/dopo e revoca negata fino a quiescenza
passate. Dieci boot consecutivi finali, stress da812 tasti, fallback senza
video e regressione console senza mouse passano (13 boot finali totali).
Pixel, IRQ e budget fisico ripristinato verificati; suite C/ASan, Rust,
Python e cppcheck passate. Il puntatore grafico e nel blocco successivo sopra.
[Contratto, debug e prove](../kernel/console/video/documentation/mouse-integration.md).

La shell ha ora anche un percorso video nel profilo esplicito `-ConsoleShell`:
prompt, editor italiano UTF-8, risposte di help/echo/status/exit, wrap/scroll,
riga corrente conservata durante le risposte e fallback seriale se il video
manca. Un servizio RAM serializzato alimenta il worker esistente; gruppi
di massimo otto celle con controllo completo delle risorse live.
[Contratto della console](../kernel/console/video/documentation/shell-design.md).
Il cursore di testo è ora una sottolineatura fissa separata dai caratteri:
Rust valida la posizione, il worker ripristina la vecchia cella e disegna
la nuova, senza timer o task aggiuntive. Corretto anche un panic intermittente
allo stop PS/2, con regressione host prima/dopo e gestione limitata dei byte
residui prima dell'ACK. Suite console/PS2/framebuffer con ASan e cppcheck,
22 boot finali:10 video,10 ordinari, uno senza video e uno stress da812 tasti.
Budget fisico ripristinato; immagini e limiti nel
[report corrente](../kernel/console/video/documentation/caret-validation.md).
Le descrizioni "shell solo seriale" nei checkpoint sotto valgono per quelle
immagini storiche. Il profilo ordinario rimane distinto; GUI, hardware fisico
e sessione senza le guardie diagnostiche v0 non sono completati.
Checkpoint mouse PS/2 collegato al kernel nel profilo separato `-Ps2Mouse`:
movimento e tre pulsanti, decoder Rust, task3, IRQ12/vettore42h, due code
con source/token distinti e quattro stack pinned. Dieci boot consecutivi
dell'ultima immagine verificano35 eventi mouse per boot insieme alla
tastiera, IRQ durante il worker, idle, stop e budget20.460 ripristinato.
Passa anche un boot con mouse e shell seriale italiana (98 tasti,10 comandi).
Host C/ASan e Rust, registry/frame, accounting quattro lease e mock EOI passano.
Nessun puntatore grafico, rotella o certificazione hardware fisico introdotti.
[Contratto](../kernel/drivers/input/ps2/mouse/documentation/runtime-design.md) e
[prove, hash e limiti](../kernel/drivers/input/ps2/mouse/documentation/runtime-validation.md).

Corretto il fail-stop temporale del worker video, riprodotto con rallentamento
TCG dopo l'avvio: il disegno era valido, ma una misura oltre100ms marcava il
servizio Failed. Scelta utente: nella console operativa WARN e contatore dei
gruppi lenti; nelle API diagnostiche il limite rimane terminale. Errori di
draw, ownership, clock e accounting restano terminali; nessuna soglia alzata.
La prova prima/dopo verifica WARN, progresso input, pixel, cleanup e memoria.
Dieci boot finali consecutivi e un boot stress da812 tasti passano con
budget20.419 ripristinato, pixel e cleanup verificati sulla stessa immagine.
La suite include ora anche19 test Python dei runner, oltre a C/ASan, Rust
e cppcheck. Il vecchio dump concorrente non conteneva lo stato del misuratore:
e compatibile con questa causa, senza attribuzione retroattiva certa.
[Correzione e limiti](../kernel/console/video/documentation/latency-fix.md);
[checkpoint diagnostico](../kernel/console/video/documentation/worker-diagnostics.md).

Checkpoint mouse precedenti: decoder host e preparazione AUX senza
streaming; le frasi "mouse non operativo" valgono per quelle immagini.
[Preparazione](../kernel/drivers/input/ps2/mouse/documentation/controller-validation.md),
[decoder](../kernel/drivers/input/ps2/mouse/documentation/validation.md).

Checkpoint precedente: corretto il rollover one-shot32 HPET riprodotto nel ramo idle.
Contatore fermo fra le attese; un IRQ di rollover anticipato non consuma la
deadline; il match maturato durante W1C viene riconosciuto senza risveglio
perso. Status/config estranei, duplicati e MMIO fallita restano errori.
Test prima/dopo, suite host C/ASan e Rust, cppcheck e 32 boot finali passano:
10 stress/fallback, 10 video, 10 ordinari, una sessione da 812 eventi tastiera
e un idle prolungato con 837 scadenze e attraversamento reale del contatore.
Budget fisico ripristinato; review APPROVE per il fix nel profilo QEMU,
senza certificare hardware fisico o assenza di altri bug futuri.
Le campagne storiche fallite e quelle intermedie interrotte restano conservate.
[Correzione, hash e prove](../arch/x86/kernel/timer/hpet/watchdog/documentation/idle/rollover.md).

## Cosa e oggi

Nucleo x86-64 UEFI su QEMU/OVMF (q35/TCG, BSP). Il loader consegna un
ELF, esce dai Boot Services e lascia paging con NX. Il kernel verifica
il contratto, possiede allocatore fisico, mapping RW/NX, descrittori,
IDT, ACPI/MADT, APIC, UART, HPET/LAPIC e uno scheduler cooperativo a
capacita fissa. Si arresta su `KERNEL_READY` oppure su un ciclo timer
continuo con stop esterno. Non e un sistema permanente di servizi:
userspace, syscall, filesystem e hardware fisico restano fuori.

## Verificato, con limiti dichiarati

- IRQ hardware 0x41 durante una task worker con IF=1 nel frame; entry
  Assembly, stop della source ed EOI prima della notifica. Le mutazioni
  Owner e coda Rust vengono mutati con IF=0 nei rispettivi contesti verificati. Non e preemption.
- Wait/epoca, idle supervisionato (STI/HLT/CLI sul dispatcher), stop
  cooperativo e due sessioni idle riusate. Report: [dispatcher](../kernel/scheduler/idle/documentation/dispatcher-validation.md).
- Ciclo timer continuo con stop esterno e cleanup. Il profilo ordinario
  resta una sequenza finita di carichi fidati; non equivale a un runtime
  operativo permanente. Zero/riuso e probe memoria sono ora nel profilo
  esplicito `-TestBootstrapMemory`, escluso dall'immagine ordinaria.
- Rust possiede stati, generazioni, coda, wait/stop e pending IRQ.
  C possiede lease, mapping e integrazione. Assembly possiede switch e
  entry IRQ. La toolchain e MSVC x64 + rustc `x86_64-pc-windows-msvc` +
  MASM; la complessita e un costo da misurare, non un vantaggio a priori.

Immagine dispatcher verificata (8 ottobre): SHA256
`1DEBCDD6548522F51C93BD417D2F6BC30C2D37199CABEC8FED57A29A971DBC47`.
Dieci boot positivi e nove negativi isolati; ciclo continuo distinto.
Coverage linee/branch N/D; Valgrind non eseguito; SMEP/SMAP/KPTI non
implementati.

## Confermato in questa verifica

- Prima della separazione `kernel/main.c` aveva 400 righe; ora ne ha 193.
  Inizializzazione in `kernel/bootstrap/`, prove in `test/kernel/bootstrap/`.
  Ownership/NX restano nel percorso ordinario. [Verifica della separazione](../kernel/bootstrap/documentation/memory.md):
  dieci boot consecutivi per profilo e matrice diagnostica di 23 scenari.
- Contraddizioni al presente, ora etichettate o corrette: [verified-state](review/verified-state.md)
  (2 ottobre: il kernel non veniva eseguito), [bootstrap](kernel/bootstrap.md),
  [core scheduler](../kernel/scheduler/documentation/core.md),
  [integrazione v0](../arch/x86/kernel/scheduler/documentation/integration.md),
  [README boot](boot/README.md).

## Fuori dal perimetro attuale

Preemption, ring 3, syscall, user copy, SMP/AP, input ordinario illimitato,
seconda toolchain Linux, `reuse/`. La coda payload IRQ a 256 byte e verificata
con dati diagnostici: [report](../kernel/scheduler/payload/documentation/validation.md).
Il primo parser PS/2 scan set 2 e ora implementato in Rust, con ABI C
senza puntatori e prove sintetiche host/kernel. Il controller ha discovery
ACPI e negoziazione verificata nel profilo isolato `-TestPs2Controller`,
con scanning/IRQ disabilitati alla fine. Il profilo `-Ps2Input` collega ora
IRQ1, coda Rust e task consumer fino a Esc, con idle HPET e cleanup:
[contratto runtime](../arch/x86/kernel/drivers/input/ps2/documentation/runtime.md),
[verifica del collegamento](../arch/x86/kernel/drivers/input/ps2/documentation/validation.md).
Il layout italiano aggiunge modificatori Rust, accenti, AltGr e UTF-8;
L'editor raccoglie fino a256 byte UTF-8, cancella scalar completi con
Backspace e consegna la riga alla task su Enter; Tab resta un evento.
Buffer pieno: rifiuto del nuovo carattere, con avviso. La task shell separata
riceve righe tramite FIFO4, con backpressure e rifiuto dei comandi troncati.
Builtin help/echo/status/exit, output seriale; exit chiude la sessione.
[Contratto shell](../kernel/shell/documentation/shell.md).
Shell verificata:24 test Rust sia debug sia release, C/ASan, accounting
di tre stack, dieci boot shell finali e dieci ordinari; stress612 press.
[Contratto editor](../kernel/console/line/documentation/editor.md);
[snapshot testo](../kernel/input/keyboard/documentation/text.md).
Editor verificato con20 test Rust sia debug sia release, test C/ASan,
dieci boot input finali, dieci ordinari e stress di559 press:
overflow rifiutato, righe da256 byte e budget ripristinato.
Resta una sessione BSP con guardie di budget.
Il contratto del parser resta in
[contratto](../kernel/drivers/input/ps2/documentation/design.md).
[Verifica controller](../kernel/drivers/input/ps2/documentation/controller-validation.md):
snapshot del blocco controller, precedente al collegamento input.

## Sequenza di lavoro

La pipeline input e ora separata per responsabilita: protocollo e decoder
in `kernel/drivers/input/ps2/`, layout in `kernel/input/keyboard/`, editor in
`kernel/console/line/`, comandi/coda in `kernel/shell/`. Backend IRQ e
orchestrazione restano in `arch/x86/kernel/drivers/input/ps2/`; gateway ABI Rust
in `kernel/input/integration/`. [Struttura e verifica corrente](../kernel/drivers/input/ps2/documentation/structure.md):
24 test Rust debug/release, C/ASan, 10.000 cicli controller mock,
10 boot input e 10 ordinari, stress612 pressioni. Hardware fisico,
riavvio dopo guasto e sessione ordinaria illimitata non sono completati.

1. Consolidamento documentale completato.
2. Separazione bootstrap memoria/diagnostica completata, con i controlli
   di sicurezza nel percorso ordinario.
3. Primo input PS/2 collegato nel profilo esplicito:
   IRQ → coda → decoder → layout italiano → output UTF-8 osservabile.
   Editor e shell separata nel profilo esplicito. Integrazione ordinaria e sessione senza guardie
   diagnostiche restano lavoro successivo.
4. Percorso minimo verso ring 3: spazio utente, stack, ingresso/uscita,
   una syscall, fault.

Il limite di file serve a separare responsabilita, non a spezzare un
modulo in pezzi artificiali. Una toolchain Linux e una task dedicata
successiva, scelta esplicita, non un adattamento silenzioso.

## Framebuffer: handoff v4 collegato

Scelta utente: cercare e impostare un modo video supportato.
Prima fase: selezione/validazione in C e Rust nel probe EFI separato,
4 test Rust debug/release, test C con ASan e accounting, dieci boot
QEMU consecutivi positivi e un caso senza video.
[Snapshot della prima fase](../kernel/drivers/video/framebuffer/documentation/validation.md).
Il framebuffer del vecchio OS e stato letto senza modificarlo:
[parti recuperabili e correzioni](../kernel/drivers/video/framebuffer/documentation/reuse-assessment.md).

Blocco pixel: offset validati senza accettare padding come pixel, buffer
corti respinti e colori RGB/BGR con byte riservato zero. Otto casi Rust
distinti debug/release, ABI C con ASan, dieci boot probe positivi e un
caso senza video sull'immagine di quel blocco.
[Snapshot pixel](../kernel/drivers/video/framebuffer/documentation/pixel-validation.md).
Handoff v4 collegato al loader e al kernel:208 byte, descrittore opzionale
da40 byte all'offset168. Validazione, esclusione della RAM, conservazione
nei retry ExitBootServices e ripristino video nell'abbandono del boot.
Dieci boot finali con video e dieci senza raggiungono KERNEL_READY.
Dieci boot della shell PS/2 verificano la regressione sullo stesso loader.
[Prove del blocco handoff](../kernel/drivers/video/framebuffer/documentation/handoff-validation.md).
Il servizio GUI futuro richiedera video valido. Mapping live, singoli pixel,
rettangoli e testo dal kernel sono descritti nei blocchi sotto; una console
video operativa completa resta il passo successivo.

Preflight del mapping collegato: Rust calcola pagine, offset e budget;
C pubblica solo un risultato valido. Il kernel usa i limiti fisici reali.
Cinque nuovi casi Rust debug/release,41398 controlli C ASan; campagne
finali in serie10/10 con video e10/10 senza. Nessuna PTE o store video.
Durante una coppia di campagne concorrenti era stato osservato un panic
del watchdog in fase ARM. Il fix del9 ottobre ferma il contatore HPET
posseduto durante la programmazione e lo riavvia dopo le verifiche.
Riproduzione host prima/dopo,30 boot finali positivi (20 con due VM
contemporanee) e10 con IRQ LAPIC perso/cleanup atteso verificati.
La ESP ordinaria contiene ora il kernel corretto gia testato e il preflight;
backup del kernel precedente conservato. Il backend video live era ancora
da fare in quel blocco; ora e verificato come descritto sotto.
[Correzione e prove](../arch/x86/kernel/timer/hpet/watchdog/documentation/arm-race.md).
[Risultati e fallimento osservato](../kernel/drivers/video/framebuffer/documentation/mapping/validation.md).

Inventario esteso in sola lettura per un framebuffer e LAPIC, con tabelle
kernel da lease pinned, guardie, esclusione degli alias su Capacity e ID
dei lease conservati nello snapshot. Test host del contratto e regressione
ordinaria di quel blocco restano distinti dal riscontro video live successivo.
Sei casi Rust debug/release, otto gruppi C ripetuti100 volte con ASan,
suite host kernel e20 boot finali ordinari (10 video,10 assente) passano.
[Contratto e verifiche](../arch/x86/mm/paging/documentation/video-resources.md).
Il gate del mapper live e chiuso nel profilo QEMU BSP; anche la scrittura
dei singoli pixel e ora verificata nel successivo blocco sotto.

Mapping framebuffer reale collegato al kernel: ramo dedicato PML4[225],
guardie, UC forte, supervisor RW/NX, tabelle da lease pinned. Rust costruisce
le entry; C possiede pubblicazione e rollback. CR3 resta lo stesso, con
invalidation delle traduzioni e delle viste ricorsive prima del rilascio.
Revoca incompleta: pagine trattenute, errore e cleanup ritentabile.
Sette gruppi C x100 con ASan/accounting, tre casi Rust debug/release,
Clippy, cppcheck senza diagnostiche bloccanti e suite host kernel passano.
Trenta boot finali:10 diagnostici con unbind/budget/rebind,10 ordinari con
video live,10 senza display. Il monitor legge fisicamente tutte le768
foglie video e le guardie; CR3 e policy UC sono verificati indipendentemente.
Nessuno store pixel del kernel era provato nel blocco mapping; la prova
pixel successiva e descritta sotto. Hardware fisico non testato.
La ESP contiene l'immagine ordinaria live gia verificata; hash della copia
confrontato, backup del kernel precedente conservato in kernel/build/.
[Contratto](../arch/x86/kernel/drivers/video/framebuffer/documentation/mapping.md)
e [prove finali](../arch/x86/kernel/drivers/video/framebuffer/documentation/validation.md).

Scrittura pixel collegata al mapping kernel: controlli Rust su coordinate,
stride, Used e colori; C richiede owner originale, lease pinned, CPU e
inventario validi prima dello store UC volatile32 bit e del readback.
Offset fisico non allineato alla pagina e padding verificati host; nessuna
allocazione aggiunta. Il probe dei cinque colori gira soltanto nel profilo
diagnostico, non nel boot ordinario. Il renderer batch del blocco successivo
e descritto sotto; nessuna GUI ancora.
Undici casi Rust per profilo debug/release e Clippy; tre gruppi pixel C
ASan oltre ai sette gruppi lifecycle x100, cppcheck senza errori bloccanti.
Trenta boot finali:10 con cinque pixel verificati fisicamente e nel PPM,
10 ordinari e10 senza display. RGB e BGR coperti host; QEMU usa BGR.
[Contratto, prove e riproduzione](../arch/x86/kernel/drivers/video/framebuffer/documentation/pixels.md).

Rettangoli e testo UTF-8 ora usano una sola verifica completa del mapping
per operazione, con readback scalare UC e autorita finale. Rust valida
geometria, limite4096 pixel e scalar Unicode; C prepara fino a64 celle8x8
da un font del chiamante e le scrive. Accenti italiani precomposti coperti,
glifi assenti sostituiti da un bitmap visibile. Input malformato, font errato
e coordinate fuori schermo vengono respinti prima degli store. Non e ancora
la console della shell: il font di dieci glifi e i disegni restano diagnostici.
Nessuna nuova allocazione o modifica del vecchio OS.
Quindici casi Rust per profilo debug/release e Clippy, tre gruppi drawing
x100 con ASan oltre alla regressione pixel/lifecycle, cppcheck13 TU senza
diagnostiche bloccanti. Trenta boot finali:10 diagnostici con2245 pixel
verificati fisicamente e nel PPM,10 ordinari,10 senza display. Font completo,
console operativa, hardware fisico e latenza IRQ restano da verificare.
[Contratto e prove del renderer](../arch/x86/kernel/drivers/video/framebuffer/documentation/drawing.md).

Font MikeSW Console derivato offline da Cascadia Mono v2407.24, con
copyright/licenza OFL e sorgente TTF preservati.109 glifi8x16: ASCII,
accenti italiani maiuscoli/minuscoli, euro e fallback. Il renderer accetta
Height8/16 esplicita e mantiene il budget4096 pixel (32 celle8x16).
Nessun parser TTF nel kernel; il collegamento alla console shell resta
successivo.16 test Rust per profilo, quattro gruppi drawing x100 con ASan
e regressioni pixel/lifecycle, cppcheck14 TU.30 boot finali:10 diagnostici
con16837 pixel fisici/visibili verificati,10 ordinari,10 senza display.
[Contratto font](../arch/x86/kernel/drivers/video/framebuffer/documentation/font.md).

Nucleo console video in kernel/console/video/: griglia RAM posseduta da un
solo chiamante, cursore Rust, wrap differito, LF/CR, Backspace, scroll e
dirty peek/ack. Massimo240x67, riserva66360 byte, nessuna allocazione.
Quattro casi Rust debug/release e Clippy,374635 CHECK C/ASan con10000 riusi,
quattro test del wrapper Python e regressione host framebuffer passano.
30 boot completati:10 diagnostici con probe griglia/raster font precedente,
10 ordinari e10 senza display. Un primo tentativo senza display ha fallito
per timeout di chiusura QEMU dopo KERNEL_READY; archiviato separatamente,
timeout host reso esplicito e misurato, nuova campagna10/10 passata.
Questo nucleo e compilato soltanto nel diagnostico: output grafico della
shell, gateway restore IF e misura latenza IRQ restano il prossimo blocco.
[Contratto](../kernel/console/video/documentation/design.md) e
[prove e limiti](../kernel/console/video/documentation/validation.md).

Gateway console per cella ora collegato nel diagnostico: owner framebuffer
privato, restore IF originale, flusher con budget massimo16 e dirty confermato
soltanto dopo successo.72 celle disegnate,56 con IF iniziale1; nuovi test host
su retry/rientro/overflow e2228226 casi scalar con IRQ/clock mock passano.
30 boot finali:10 diagnostici con26053 pixel fisici/visibili verificati,
10 ordinari e10 senza display. Il limite precedente sul gateway e superato;
A quel checkpoint restavano output shell e prova di latenza con sorgenti PS/2/watchdog attive.
I tick TSC grezzi non sono una deadline o una misura esatta del tempo IF0.
La console rimane diagnostica, la shell seriale; ESP invariata in questo blocco.
[Contratto](../kernel/console/video/documentation/rendering.md) e
[verifica finale](../kernel/console/video/documentation/rendering-validation.md).

Convivenza risorse console verificata nel profilo isolato TestConsoleActive:
video inizializzato prima del watchdog, validatore dell'owner privato usato
dalle sole interfacce necessarie, senza modificare le API paging precedenti.
HPET esclude tutta l'apertura video; lease pinned verificati anche durante
input/idle/cleanup.576 controlli gateway e1100 policy HPET C/ASan passano,
con regressioni console/framebuffer/watchdog e wrapper Python.
40 boot finali:10 combinati con tastiera italiana e shell seriale,10 grafici,
10 ordinari,10 senza display. Il profilo TestConsoleActive non disegna;
quel checkpoint precede la verifica del rendering descritta sotto. La finestra IRQ delle
task rimane quella Assembly gia verificata, senza ampliamento a C arbitrario.
[Piano](../kernel/console/video/documentation/active-design.md) e
[prove finali](../kernel/console/video/documentation/active-validation.md).

Rendering con input ora verificato nel profilo isolato TestConsoleDraw:
una cella per passo, clock PM e policy Rust; snapshot diagnostico derivato
dai soli48 frame dei tre stack originali/pinned, scansione completa del
renderer e ripristino dell'inventario baseline. Dieci boot finali con
3548 celle totali,128 pixel fisici/visibili finali verificati per boot,
zero perdite osservate, ack HPET tra aggiornamenti e budget20438 ripristinato.
Altri10 boot ordinari e10 senza video passano sull'immagine di regressione.

L'obiettivo10ms è fallito e rimane aperto:17 celle oltre10ms, massimo53,37ms
per flush nella campagna finale. Gate diagnostico100ms, coerente col poll
input v0; non certifica latenza end-to-end né protegge da uno stallo C IF0.
Il rendering resta diagnostico; shell seriale. Prima di aumentare il lavoro
video serve ridurre il costo senza perdere ownership; le misure per fase
sono ora disponibili nel checkpoint sotto.
[Design](../kernel/console/video/documentation/measured-design.md) e
[prove, tentativi falliti e limiti](../kernel/console/video/documentation/measured-validation.md).

Profilazione completata: 3543 celle in dieci boot finali; inventario 94,34%
del tempo profilato, massimo flush 7,31ms in questa campagna. Altri dieci
boot ordinari e dieci senza video passano. Nessuna ottimizzazione applicata
né risoluzione dimostrata dei picchi precedenti. Scelta utente per il prossimo
blocco: gruppi fino a8 celle adiacenti, controlli completi per gruppo; ora
implementati nel checkpoint successivo.
[Prove e limiti](../kernel/console/video/documentation/profile-validation.md).

Batch fino a8 celle implementato, con una scansione completa per gruppo,
verifica finale e ack solo dopo successo. Quaranta boot finali:10 batch,
10 singoli,10 ordinari e10 senza video. Batch:28480 celle,3560 gruppi,
zero perdite input osservate,1024 pixel fisici/visibili finali verificati per
boot e budget20436 ripristinato. Costo medio profilato per cella:0,302ms
contro2,018ms nel singolo; confronto QEMU non controllato, non garanzia hardware.
Quattro gruppi oltre10ms, massimo15,10ms; gate diagnostico100ms passa.
Shell ancora seriale e nessuna estensione IRQ a C arbitrario.
[Contratto](../kernel/console/video/documentation/batch-design.md) e
[prove finali](../kernel/console/video/documentation/batch-validation.md).
