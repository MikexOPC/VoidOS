# Stato corrente — 10 ottobre 2026

Questo e il riferimento breve dello stato reale. I checkpoint in
`docs/review/` e i report di modulo restano evidenza del blocco in cui
sono stati scritti; se una frase al presente li contraddice, vale questo
file.

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
  Inizializzazione in `kernel/bootstrap/`, prove in `kernel/test/bootstrap/`.
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
blocco: gruppi fino a8 celle adiacenti, controlli completi per gruppo.
[Prove e limiti](../kernel/console/video/documentation/profile-validation.md).
