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
[Design per CPU](../../arch/x86/kernel/cpu/percpu/documentation/design.md).

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

## Backend x86

I documenti completi precedenti sono nelle cartelle dei moduli x86 al
commit `8bad38a`.

### Staging ELF del loader

Il profilo identity del loader ammette staging allineato a 4 KiB,
con base almeno 4096 e fine esclusiva entro 2^47. Il validatore
controlla numero di pagine, moltiplicazione e somma prima che il
loader azzeri o copi. Le allocazioni distinte sono registrate per
consentire il rollback anche se il controllo fallisce. Un rilascio
fallito resta ritentabile. Un alias ambiguo viene rifiutato e
trattenuto. Riferimenti UEFI 2.10 §7.2.1 e Intel SDM Vol. 3A §4.5.

### Contesti per CPU, avvio AP e IPI

Il registro di boot distingue indice logico, ID APIC e UID ACPI e
ammette al massimo 64 CPU. GS identifica il contesto dei lock della
CPU. Le attese usano il clock ACPI PM con un limite di iterazioni.
Ogni AP ha stack, IST, GDT/TSS e contesto privati. Il BSP prepara
la riserva bassa, la root inattiva, il trampolino e i record prima
di sigillare il gruppo. Il codice e la pagina dati d'ingresso sono
distinti. Dopo il punto irreversibile, un guasto trattiene le lease.
L'ammissione lega identità e generazione al contesto privato. Il BSP
riarma la pagina dati soltanto dopo la conferma di parcheggio.

Nel boot ordinario xAPIC gli AP parcheggiati servono PING e flush TLB.
Il BSP modifica le mappature e aspetta gli ACK della generazione
corrente, pubblicati dopo EOI, prima del riuso. Ogni CPU ricarica CR3
con PGE e PCIDE disabilitati. L'handler non alloca e non prende lock.
Il panic coordinato elegge un reporter e usa NMI, record privati e
attese limitate. I rendezvous di prova e i guasti iniettati sono nei
profili separati. Per il flush, Intel SDM Vol. 3A §§4.10.4–4.10.5.

### Scheduler, LAPIC e paging

Gli stack delle task hanno guardie e lease. Il backend torna al
dispatcher prima del rilascio e attende lo shootdown se gli AP
sono attivi. L'ordinaria usa lo switch diretto. Il probe di
preservazione appartiene al profilo dedicato.

La calibrazione LAPIC delimita i campioni con il clock ACPI PM,
richiede due stime compatibili e limita i tentativi. Arm→IRQ e
ritorno dal watchdog sono misurati separatamente. L'inventario
paging valida foglie, permessi, guardie e lease pinned per RAM e
MMIO. La verifica K3-H usa un percorso lineare per mappe ordinate
e il confronto completo quando l'ordine manca. Gli alias delle
tabelle si cercano in un array locale ordinato, senza cache delle
autorizzazioni.

### Descrizione HPET e watchdog

Il kernel cerca la tabella HPET nella directory ACPI validata, copia
la descrizione e verifica capacità e periodo con un mapping temporaneo
in sola lettura. Il probe osserva fino a 32 comparatori. La discovery
distingue assenza e input invalido e lascia i timer disarmati.

Il watchdog possiede mapping, comparatore, route IOAPIC e IRQ fino
allo stop confermato. Seleziona una route ammessa, arma one-shot a
32 bit con readback e distingue scadenza da rollover. Su guasto
registra la fase, limita i retry e trattiene l'owner se il cleanup
non è confermato. Il boot ordinario lo usa sul BSP. Perdita LAPIC,
route alternative, recovery e rollover hanno profili di prova
separati. Fonti hardware nei sorgenti e negli originali, tra cui
Intel HPET 1.0a §§2.3.4–2.3.9 e §2.4.3.1.

### Video, VT-d e PS/2

Il framebuffer adotta metadati GOP validati e usa mapping UC con
lease e guardie. Limita coordinate, stride, area e formati RGB/BGR.
Testo e rettangoli usano batch delimitati e font offline. L'ordinaria
prepara il framebuffer se presente. Disegno e console interattiva
sono provati in profili separati.
[Font e cella](../../kernel/console/video/documentation/font-cell.md).

La console VirtIO con VT-d ha un dominio per la GPU. La presentazione
separa submit e polling, applica una scadenza e gestisce fallback GOP
e recupero. Reset e invalidazione precedono il rilascio. La geometria
segue la superficie attiva. GPU fisiche, più unità e RMRR sono fuori
dalle prove. Il resolver GPU–DRHD usa topologia PCI validata e scope
DMAR, con precedenza degli scope espliciti su INCLUDE_PCI_ALL dello
stesso segmento. Respinge ambiguità. Routing in sola lettura e
isolamento DMA hanno prove distinte. Intel VT-d 5.0 §8.3.1.

Nella sessione PS/2 il consumer scarta l'input incerto e azzera
decoder, modificatori e riga dopo un guasto recuperabile. Violazioni
di ownership o errori oltre il limite terminano la sessione.
[Runtime PS/2](../../arch/x86/kernel/drivers/input/ps2/documentation/runtime.md).
