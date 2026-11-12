# Design del consolidamento

Versione completa precedente in `docs/design/k1-higher-half.md`, `docs/design/g7-smp-user.md`, `docs/design/project-direction.md` e `docs/design/k6-design.md` (Migrazione della build), al commit `efecb8b`.

Il consolidamento porta boot, shell e processi nello stesso runtime,
con build riproducibile e scheduler preemptive su tutte le CPU. Qui sono
raccolti i contratti già adottati. L'immagine unica e lo scheduler di
produzione richiedono ancora alternative e scelta dell'utente.

## Layout e risorse

`arch/x86/configs/layout.h` definisce gli intervalli condivisi fra
loader, kernel e strumenti. L'immagine del kernel è nello slot 511 da
`0xFFFF_FFFF_8000_0000`, il ricorsivo nello slot 510. Mappa e contratto
di boot usano 479–480, dispositivi, heap e AP usano 481–487. Lo slot 488
contiene stack, finestra di pagina, arena e LAPIC. Il kernel rimuove lo
slot basso della capsula dopo aver installato e verificato stack,
descrittori e root. Le pagine del loader restano riservate.

Le lease hanno un proprietario esplicito. Una risorsa con mapping
attivo o rilascio incerto resta pinned. Dopo l'avvio degli AP, il
riuso dei frame dopo un unmapping aspetta gli ACK del flush TLB.
Il percorso di errore trattiene la risorsa se un ACK manca.
[Contratto di revoca](../../arch/x86/mm/paging/retirement/documentation/design.md).

## Lock, CPU e scheduler

Il ticket lock con salvataggio di IF usa stato per CPU, rango e rilascio
LIFO. Le sezioni critiche sui metadati di memoria evitano hardware e
callback. GS identifica il contesto per CPU. Per gli AP il BSP prepara
risorse private, verifica e sigilla il gruppo prima di INIT/SIPI e
conserva le lease dopo il punto irreversibile. Gli AP ammessi rispondono
a PING e shootdown. Il panic coordinato usa NMI e attese limitate.
[Design per CPU](../../arch/x86/kernel/cpu/percpu/documentation/k3-design.md).

La scelta K4-C è la coda globale con lock, stato e affinità in Rust,
dispatcher per CPU e switch all'uscita dell'IRQ in Assembly. Preemption
solo da una task con IF=1 e senza lock tenuti. La slice iniziale del
design era 10 ms, soggetta a misura. L'ordinaria esegue i task cooperativi
sul BSP, mentre i driver e gli IRQ dei dispositivi restano sul BSP.
Il profilo shell inoltra un solo job fidato ad AP1 e raccoglie lo stack
dopo la quiescenza. [Design K4-C](../../kernel/scheduler/documentation/k4c-design.md).

## Oggetti driver

L'oggetto ha un owner esplicito e dichiara il proprio contesto di
esecuzione. Lease, mappature e vettori appartengono all'oggetto e si
rilasciano dopo aver fermato la sorgente. Clock, allocatore e altre
dipendenze arrivano come parametri. La sincronizzazione dipende dai
contesti che accedono allo stato, compresi IRQ e altre CPU. G6 applica
queste regole alla presentazione, alla sessione VT-d e alla console.

## Build e migrazione

Meson costruisce il grafo Ninja dal piano risolto dallo strumento Rust
in `scripts/tooling/build-plan/`. `kernel-build` esegue i passi, genera
le dipendenze, controlla le copie del layout, collega il PE e lo converte
in ELF. C e Assembly usano MSVC/MASM, il trampolino usa NASM e il
programma incorporato usa il target Rust `x86_64-unknown-none`.
I depfile derivano da `/sourceDependencies` per C e da `--emit=dep-info`
per Rust. Gli include MASM sono rifiutati perché il tool non ne produce
le dipendenze. Il link usa `/Brepro`.

Il runner Rust gestisce la VM posseduta, le scadenze del monitor, i log
e il confronto fra esecuzioni. La migrazione delle suite host procede
per gruppi verificando conteggi, esiti e casi negativi. Il bootloader
resta sullo script PowerShell. I profili diagnostici disponibili solo
in `build.ps1` vengono ritirati quando la prova equivalente gira
altrove. Restano il percorso di build a un comando e la CI con test host e
boot QEMU. [Istruzioni di build](../../README.md).

Il debito aperto comprende i limiti fissi di task, IRQ e heap, i driver
sul BSP, la duplicazione delle attese con deadline e della geometria nei
runner, e i percorsi diagnostici nel kernel. Il registro mantiene le
voci su console, VT-d e strumenti. [Debito tecnico](../future-work.md).
