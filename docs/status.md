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
Il servizio GUI futuro richiedera video valido; mapping e rendering
dal kernel restano il prossimo blocco, non sono gia implementati.

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
backup del kernel precedente conservato. Il backend video live resta da fare.
[Correzione e prove](../arch/x86/kernel/timer/hpet/watchdog/documentation/arm-race.md).
[Risultati e fallimento osservato](../kernel/drivers/video/framebuffer/documentation/mapping/validation.md).

Inventario esteso in sola lettura per un framebuffer e LAPIC, con tabelle
kernel da lease pinned, guardie, esclusione degli alias su Capacity e ID
dei lease conservati nello snapshot. Test host del contratto e regressione
ordinaria restano distinti dal futuro riscontro hardware video live.
Sei casi Rust debug/release, otto gruppi C ripetuti100 volte con ASan,
suite host kernel e20 boot finali ordinari (10 video,10 assente) passano.
[Contratto e verifiche](../arch/x86/mm/paging/documentation/video-resources.md).
Il mapper live e il writer dei pixel non sono ancora implementati.
