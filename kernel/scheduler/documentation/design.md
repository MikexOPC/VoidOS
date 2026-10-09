# Scheduler cooperativo v0 — progetto

Data: 8 ottobre 2026. Baseline: `14649ba` pubblicato su origin/main.
Scelta autorizzata: scheduler cooperativo BSP, task kernel, Rust per stati/coda,
C per risorse e integrazione, Assembly per lo switch. Questo blocco definisce
il progetto: non aggiunge codice eseguibile e non dichiara uno scheduler pronto.

Questa introduzione descrive il blocco documentale originale. Il successivo
[core Rust/FFI](core.md), il backend nativo e il dispatcher sono ora implementati.
Le prove e i limiti del profilo v0 sono nel
[rapporto finale](../../../arch/x86/kernel/scheduler/documentation/integration.md).

## Traguardo

Due task su stack distinti alternano lavoro e yield, conservano contesto e
variabili locali, terminano e restituiscono il controllo al dispatcher.
Il dispatcher raccoglie i task terminati solo dal proprio stack. Il kernel
prosegue sullo stack principale esistente, senza ritornare a quello del loader.

Documenti del modulo: [ricerca](research.md), [backend x86](context.md),
[accettazione](validation.md). Le interfacce esistenti sono state lette senza
modificare allocatore, paging, interrupt, timer o bootloader.

## Perimetro e decisioni v0

| Tema | Decisione |
|---|---|
| Esecuzione | BSP singolo, ring 0, stesso spazio di indirizzi kernel |
| Politica | FIFO tra task pronti; yield reinserisce in coda |
| Capacita | Otto slot task, dispatcher separato dagli slot |
| Identita | Coppia indice u32 e generazione u64; zero non valido |
| Stack | 64 KiB per task preparato, guard inferiore/superiore da 4 KiB |
| Switch | Task -> dispatcher -> task; mai una CALL ordinaria al posto dello switch |
| Terminazione | Ritorno della entry trasformato in exit, stato FINISHED |
| Scheduling | Solo yield ed exit volontari; nessuno switch da IRQ |
| Interrupt | IF=0 durante questo profilo finito e ogni transizione |
| Lifetime | Nessuna creazione/cancellazione da task o callback IRQ |
| Raccolta | Solo dispatcher, dopo ritorno effettivo dal task |
| Heap | Nessun heap Rust; tabella e coda a capacita fissa |

Otto slot e 64 KiB sono budget v0 del progetto, non requisiti hardware o valori
ottimali misurati. Si preparano solo gli stack richiesti, non tutti gli otto.
I task sono codice kernel fidato: lo scheduler non isola task ring 0 malevoli.

Il prototipo finito tiene IF=0 anche durante il lavoro dei task. Non rappresenta
un sistema interattivo: un task che non cede la CPU puo bloccare gli altri.
La deadline del test host rileva il blocco ma non recupera il kernel.
Le wait HLT del watchdog non vengono chiamate dai task di questo profilo.

## Ownership reale di Rust

Il core Rust possiede gli slot, le generazioni, la coda pronta e lo stato corrente.
C non riceve un puntatore scrivibile a queste strutture. Non si tratta di un
validatore scalare: le transizioni aggiornano il proprietario Rust della politica.
Il core utilizza array e indici verificati, senza raw pointer o unsafe.

Il gateway FFI e distinto dal core. Conserva un owner statico non spostabile,
accessibile solo sul BSP fuori da interrupt/NMI e con IF=0. L'accesso al singleton
richiede un piccolo confine unsafe esplicito; IF=0 non dimostra sincronizzazione
SMP. Un flag di ingresso impedisce rientro software, non sostituisce un lock SMP.
Nessun riferimento Rust o prestito del gateway sopravvive a una chiamata FFI,
a un callback C, a un'allocazione nativa o allo switch di stack.

C possiede lease fisiche, mapping e contesti nativi indicizzati dal medesimo
handle. La entry e un indirizzo RX kernel verificato; il suo argomento rimane
valido fino a raccolta completata. Nel primo test entry statiche senza argomenti.
Ogni argomento futuro avra un contratto di durata, non un void pointer anonimo.

## Stati e transizioni

| Origine | Operazione autorizzata | Destinazione |
|---|---|---|
| FREE | reserve dal dispatcher, nuova generazione non nulla | RESERVED |
| RESERVED | publish dopo stack/contesto nativi verificati | READY |
| RESERVED | abort dopo rollback nativo completamente verificato | FREE |
| READY | select, rimozione dalla coda | RUNNING |
| RUNNING | evento YIELD ricevuto sul dispatcher | READY, in fondo alla coda |
| RUNNING | evento EXIT ricevuto sul dispatcher | FINISHED |
| FINISHED | collect dopo revoca mapping e rilascio lease confermati | FREE |
| FREE con generazione massima | tentativo reserve | RETIRED |

RETIRED e definitivo: nessun wrap della generazione e nessun riuso ambiguo.
Un guasto di cleanup conserva RESERVED o FINISHED e le risorse ancora possedute.
Non si torna a FREE per nascondere un rilascio incompleto. L'incoerenza nel task
attivo richiede fail-stop, non il tentativo di eseguire il task successivo.

Invarianti:

1. Nessun duplicato nella coda; ogni READY compare esattamente una volta.
2. RESERVED, RUNNING, FINISHED, FREE e RETIRED non compaiono nella coda.
3. Al massimo un RUNNING; handle corrente e relativo record nativo coincidono.
4. Nessun task pubblicato con risorse parziali o stack di un altro task.
5. Nessun rilascio dello stack corrente o del dispatcher.
6. Un handle vecchio non autorizza un nuovo task nello stesso slot.
7. Ogni rifiuto lascia invariati gli stati, salvo il ritiro definitivo per
   esaurimento della generazione, che restituisce un esito distinto.
8. Contatori diagnostici controllati prima di incremento; nessun wrap silenzioso.

La coda circolare usa head e count limitati a otto. Gli aggiornamenti degli
indici sono O(1); l'implementazione verifica gli invarianti prima/dopo le
transizioni, quindi l'operazione completa include controlli O(N^2).
La ricerca di uno slot FREE e limitata a otto: O(N), N<=8. La verifica completa
degli invarianti puo usare O(N^2) per duplicati, al massimo 64 confronti; questo
e un limite progettuale, non un benchmark o una scelta per migliaia di task.

## Protocollo dispatcher/task

La mailbox C privata contiene handle, motivo NONE/YIELD/EXIT e stato validita.
Un solo task attivo la produce; il dispatcher la consuma. Gli IRQ non la toccano.
La mailbox viene azzerata prima dell'ingresso nel task e una doppia pubblicazione
e rifiutata. Il task non modifica direttamente la coda Rust.

Pseudocodice progettuale, non implementazione eseguibile:

```text
prepare(entry):
    reserve un handle in Rust; ritorna dalla FFI
    acquisisci stack e mapping; prepara il contesto iniziale
    se verifica fallisce: rollback; abort solo se rilascio completo
    publish READY soltanto dopo tutte le verifiche native

run():
    sul dispatcher: verifica owner, CPU, IF, mapping, mailbox vuota
    se nessun READY e nessun RUNNING: restituisci EMPTY/DONE verificato
    select in Rust; termina la FFI prima dello switch
    verifica record nativo; se fallisce non saltare a un altro task
    switch dispatcher -> task
    [ripresa reale sul dispatcher]
    verifica stack dispatcher, handle e motivo della mailbox
    applica YIELD oppure EXIT in Rust; termina la FFI
    se FINISHED: revoca mapping, rilascia lease, poi collect
    ripeti entro il budget del workload diagnostico

yield():
    verifica task attivo e stack; pubblica YIELD
    switch task -> dispatcher
    [ripresa reale sul medesimo stack task]
    verifica identita e ritorna al chiamante originale

bootstrap_task():
    ricava l'entry dal record nativo verificato
    chiama entry sul nuovo stack
    al ritorno pubblica EXIT e switch task -> dispatcher
    qualunque ripresa successiva di questo percorso e un fault previsto
```

Durante il breve ritorno al dispatcher, Rust conserva RUNNING fino al consumo
della mailbox; durante l'ingresso conserva RUNNING prima del salto. Questa fase
e privata al dispatcher: nessun osservatore IRQ/NMI legge lo stato scheduler.
Non e una pubblicazione valida per SMP o preemption.

## Risorse, cleanup e integrazione

La lease fisica identifica il proprietario, non fornisce da sola un indirizzo
virtuale utilizzabile. Il backend x86 deve riservare VA dedicate, dimostrare
assenza di collisioni, mappare solo pagine dati RW/supervisor/NX e mantenere
entrambe le guard non presenti. Non si utilizza la finestra MMIO come stack.

Le API dello stack bootstrap dichiarano commit permanente: non sono un pool di
stack task riutilizzabile. Serve un adapter del modulo scheduler con ownership
propria, revoca delle PTE e invalidazione locale prima di release/riuso. La
geometria VA e il budget delle page table saranno verificati prima di scrivere
PTE; il presente progetto non assegna indirizzi o indici PT non verificati.

Un rifiuto di publish dopo la preparazione richiede lo stesso rollback di una
failure nativa: non lasciare uno stack allocato senza slot RESERVED. Se il core
segnala owner/coda corrotti, conservare il record e fermare l'esecuzione anziche
tentare abort/collect su un proprietario non piu verificabile.

Rollback: non pubblicare READY -> revocare mapping possedute -> invalidare ->
verificare guard/assenza -> rilasciare lease -> abort RESERVED. Se uno step fallisce,
conservare le risorse e fermare il profilo diagnostico. Nessun cleanup del task
prima di essere tornati sul dispatcher e nessuna lease azzerata per forzare release.

Nel positivo finale, confrontare le pagine libere e lease vive prima della
preparazione e dopo la raccolta; eventuali page table persistenti devono avere
owner e budget separati, non essere chiamate leak zero senza accounting.

## Struttura prevista e sequenza

Il modulo funzionale e kernel/scheduler, compreso il suo adapter x86 necessario.
La policy comune non include header HPET, LAPIC, UEFI o arch/x86. Il backend
specifico andra in arch/x86/kernel/scheduler; non si rinomina arch/x86.

```text
kernel/scheduler/
    scheduler.h, dispatcher.c
    rust/state/owner.rs, queue.rs
    rust/ffi/gateway.rs
    test/state/, test/ffi/, scripts/, documentation/
arch/x86/kernel/scheduler/
    context.h, context.c, stacks.c
    assembly/switch/context.asm
    test/context/, test/stacks/, scripts/
```

Questa e una struttura prevista, non file gia implementati. Ogni cartella
funzionale manterra cinque file diretti e ogni sorgente massimo 400 righe.

1. Core Rust e FFI: stati/coda/handle, confini, modello di riferimento e rientro.
2. Adapter risorse e Assembly: layout assert, CPU/ABI, guard, rollback e switch.
3. Dispatcher e workload finito: due task, yield/exit/collect, accounting e QEMU.

I tre blocchi appartengono alla medesima task scheduler. I test host del primo
non autorizzano a dichiarare pronto il modulo: serve il gate nativo finale.
Il loop implementazione -> debug -> miglioramento -> test -> review si applica
prima di allargare il profilo a IRQ, wait, preemption o SMP.

## Upgrade path

L'estensione eventi con epoca e wake-all e stata autorizzata e implementata:
[contratto attesa/risveglio](../wait/documentation/design.md) e
[prove dell'estensione](../wait/documentation/validation.md).

Dopo il gate v0: wait su eventi con protocollo contro wake perso; poi profilo
IF abilitato e lifetime handler/stack; poi preemption con frame IRQ completo.
Ring 3 e spazi di indirizzi distinti richiedono un contratto separato di entry,
syscall e user copy. SMP richiede AP realmente avviati, locking e shootdown.
Le decisioni su priorita, quantum e algoritmi dipenderanno da workload misurati.
Questo scheduler e una base di esecuzione, non dimostra gia un kernel ibrido.
