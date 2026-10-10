# Gateway console per cella — 10 ottobre 2026

Modulo console/video/, con gateway framebuffer come interfaccia di dipendenza.
Scelta utente gia approvata: griglia RAM e aggiornamenti per cella, nessuna
task renderer. [Nucleo](design.md), precedente [verifica](validation.md).
Questo blocco collega griglia e video nel diagnostico, non ancora la shell.

## Contratto

kernel_console_render_init associa owner originale, griglia originale,
callback e contesto kernel fidati. Nessun heap. Flush budget1..16 celle,
singolo mutatore BSP, niente IRQ/NMI, SMP, preemption o callback che cedono
la CPU/modificano la griglia. Busy respinge il rientro; Self respinge copie.
La serializzazione fra input e shell separati non e implementata qui.

Peek -> callback -> ack solo su successo e durata positiva; errore restituisce0
e lascia la cella dirty, Busy viene rimosso. Celle precedenti confermate
restano completate; non si annullano pixel gia scritti. Stato e lavoro
residuo conservati per retry esplicito o fail-stop. Un wrap della scansione
recupera celle modificate prima di Next. CellsDone protegge il proprio overflow.
IDLE1/MORE2 non equivalgono a KERNEL_READY o successo dell'intera shell.

x86_fb_kernel_cell espone coordinate di cella, scalar, colori e tick senza
esporre il puntatore dell'owner statico. Gateway privato verifica owner live,
presenza video, scalar e colori, colonne<240/righe<67 e geometria effettiva;
codifica UTF-8 locale1..4 byte, nessun buffer o puntatore trattenuto.
Supporto TSC richiesto prima della misura. Per ogni cella: timestamp ordinato,
salva IF/CLI, renderer esistente con tutte le verifiche, restore IF originale
su successo e errore, secondo timestamp ordinato. IF0 resta0, IF1 torna1;
nessun POPF di flag arbitrari. Callback e output ticks devono essere RAM
kernel valida e separata dagli owner. Non e una superficie user-copy.

Il renderer mantiene il controllo completo dell'inventario, pin/guardie,
UC, readback e autorita finale,128 pixel/cella. Nessuna scorciatoia sulle PTE.
Prevalidazione fallita non disegna; failure dopo store puo lasciare una
cella parzialmente aggiornata. Non fare ack; il retry ridisegna tutti128 pixel.
Se manca video, gateway fallisce senza toccare IRQ; seriale resta disponibile.

## Fonti hardware verificate

[Intel SDM Vol.2,325383-079US,marzo2023](https://cdrdv2-public.intel.com/774492/325383-sdm-vol-2abcd.pdf),
scaricato in kernel/build/console-cell-sdm2.pdf, testo selezionato in
console-cell-sdm-extract.txt. Non presentato come l'ultima edizione SDM.

| Accesso | Fonte esatta | Prerequisito e motivo |
|---|---|---|
| CLI | Vol.2A pp.3-166..167,tabella3-7 | CPL0; RFLAGS.IF bit9 a0 durante verifica/store |
| STI | Vol.2B p.4-663 | Restore soltanto di IF; riconoscimento IRQ dopo istruzione successiva |
| CPUID leaf0/1 | Vol.2A p.3-217,tabella3-11 | Max leaf>=1; EDX[4] supporto TSC |
| CPUID prima/dopo RDTSC | Vol.2A p.3-217 | Serializza esecuzione attorno al timestamp |
| RDTSC | Vol.2B p.4-551 | CPL0 consentito anche con CR4.TSD; tick grezzi64 bit |

Posizione IF verificata anche in [Intel SDM Vol.1,dicembre2024](https://cdrdv2-public.intel.com/843827/253665-sdm-vol-1-dec-24.pdf),
§3.4.3 figura3-8 e §3.4.3.3 p.3-17: IF bit9; §3.4.3.4 per RFLAGS64 bit.
PDF conservato in kernel/build/console-cell-sdm1.pdf.

CLI/STI non bloccano NMI/eccezioni. ReadWriteBarrier e una barriera del
compilatore, non un lock SMP o una barriera hardware. Disassembly MSVC
verificato in kernel/build/console-render-native-disassembly.txt: CPUID/
RDTSC/CPUID, PUSHFQ/CLI, branch STI o CLI e controllo bit0x200. Nessun
registro CR/MSR/PAT cambiato da questo gateway. Store UC resta quello del
[renderer verificato](../../../../arch/x86/kernel/drivers/video/framebuffer/documentation/drawing.md).

Tick misurati circondano suspend/draw/resume e includono overhead di misura
e possibili IRQ prima/dopo CLI/STI; non sono la durata esatta di IF0.
Nessuna frequenza TSC calibrata/invariant-TSC assunta, conversione in ms,
benchmark hardware reale o limite di latenza IRQ dichiarato.

## Gate prima della shell

Misurare con clock appropriato e sorgenti PS/2/watchdog attive mentre si
disegna; fissare una deadline motivata, verificare byte/IRQ persi e budget
dispatcher. Il solo probe IF1 con source ferme non chiude questo gate.
Poi collegare output/echo alla griglia con un unico owner e stato UTF-8
persistente attraverso i blocchi seriali; editor mantiene il confine BS.
Questo blocco non avanza a quelle dipendenze senza la prova di latenza.
