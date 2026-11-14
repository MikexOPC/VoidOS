# Design: scheduler preemptive nell'immagine di produzione

Stato: proposta, 14 novembre 2026. Nessun codice scritto. Riprende le scelte
già approvate di K4-C ([design](../../kernel/scheduler/documentation/k4c-design.md),
[ricerca](../../kernel/scheduler/documentation/research.md)) e la parte B2 di
[immagine unica](immagine-unica.md).

## Obiettivo

Nell'immagine di produzione lo scheduler non termina mai, sospende i task
con il timer su tutte le CPU avviate e non ha limiti fissi minuscoli sul
numero di task.

## Cosa esiste oggi

| Pezzo | Dove | In produzione |
|---|---|---|
| Core Rust: stati, coda globale sotto lock, `current` per CPU, affinità | `kernel/scheduler/rust/` | Sì, usato dal BSP |
| Preemption all'uscita dell'IRQ verso il dispatcher | `arch/x86/kernel/scheduler/`, `interrupts/return/` | No: solo con flag di test |
| Dispatcher sugli AP, task concorrenti, preemption AP | `arch/x86/kernel/scheduler/batch/`, `admission/` | No: solo `KERNEL_TEST_AP_*`; in produzione solo il job `ap <n>` |
| Timer LAPIC periodico per CPU | profili `bsp-periodic`, `ap-timer` | No |
| Modello di esecuzione | sessioni in sequenza che terminano | La shell è l'ultima sessione |
| Capacità | 8 task: coda, registro IRQ e canali Rust da 8; stack in 8 posizioni da 18 pagine nella tabella delle finestre (voci 320-463) | 8 |
| Driver | console, PS/2, shell, watchdog presuppongono il BSP | BSP |

Prove esistenti dei pezzi di test: `ap-task-four` (8 task su 4 AP,
preemption), `bsp-periodic`, `ap-task-concurrent-stress`. Sono profili
diagnostici: niente di questo è provato nell'immagine di produzione.

## Alternative

**S1. Portare in produzione il meccanismo K4-C (consigliata).**
Si promuovono i pezzi già provati, in tre obiettivi in fila:

1. **Dispatcher persistente sul BSP** (è B2): una sola esecuzione dello
   scheduler che non termina; input, shell e servizi diventano task
   permanenti; un task idle fa `STI; HLT`. Preemption sul BSP con il timer
   LAPIC periodico, timeslice iniziale 10 ms da misurare.
2. **Dispatcher su tutti gli AP**: ogni AP esegue il proprio dispatcher e
   l'idle; task senza affinità vanno su qualsiasi CPU, i task dei driver
   restano legati al BSP finché i driver non sono sicuri su più CPU.
3. **Capacità**: stack dei task in una regione dedicata (uno slot PML4
   proprio, tabelle allocate su richiesta) e core Rust con capacità larga
   e motivata (proposta: 256 task) invece di 8.

- Pro: riusa codice già provato in diagnostica; rischio distribuito su
  tre obiettivi verificabili separatamente.
- Contro: resta una coda globale con un lock; con molte CPU la contesa
  andrà misurata.

**S2. Nuovo scheduler.** Code per CPU, task allocati dall'heap,
bilanciamento. Più scalabile, ma butta via K4-C e le sue prove; da
considerare solo se le misure di S1 lo richiedono. È la via che il design
K4-C lascia aperta dietro l'interfaccia `pick/enqueue`.

## Decisioni da prendere

1. **Alternativa**: S1 o S2. Proposta: S1.
2. **Ordine dei tre obiettivi di S1**: proposta 1, 2, 3. In alternativa la
   capacità per prima, se si vogliono più task prima di più CPU.
3. **Driver**: legati al BSP in questo obiettivo (proposta), oppure renderli
   sicuri su più CPU prima. Renderli sicuri è un lavoro per driver.
4. **Capacità**: 256 come limite largo motivato dalla regione degli stack
   (proposta), oppure task allocati dall'heap senza limite fisso, che
   richiede un allocatore usabile dal core Rust `no_std`.
5. **Timeslice**: 10 ms come ipotesi da misurare, come in K4-C.

## Criteri di chiusura (per S1 completa)

- Lo scheduler dell'immagine di produzione non termina; shell e input sono
  task permanenti; nessuna sessione.
- Un task che non cede la CPU viene sospeso dal timer su ogni CPU avviata
  e la shell resta reattiva.
- Almeno 64 task creati e terminati in ciclo, con budget di pagine e heap
  invariati.
- 10 boot, `ap-task-*` e campagne dell'immagine ordinaria verdi, stress di
  30 minuti con task su tutte le CPU.

## Rischi

- Il codice dei driver scritto per il modello cooperativo sul BSP: la
  preemption lo può interrompere a metà. La regola K4-C (si interrompe solo
  con IF=1 e nessun lock tenuto) lo protegge solo se i driver usano i lock.
- Passare da sessioni a esecuzione persistente cambia come vengono
  verificati budget e pulizia, oggi controllati alla fine di ogni sessione.
- La regione dedicata agli stack cambia `layout.h`, che loader, kernel e
  runner leggono insieme.

## Decisioni (14 novembre 2026)

Scelte dell'utente: S1, nell'ordine 1 (dispatcher persistente sul BSP),
2 (dispatcher sugli AP), 3 (capacità). Proposte accettate: driver legati al
BSP in questo obiettivo, capacità 256 legata alla regione degli stack,
timeslice 10 ms da misurare.

## Obiettivo 1, task 1: dati condivisi dai task della shell (analisi, 14 novembre 2026)

I quattro task della sessione PS/2 sono 0 tastiera (`x86_ps2_consume`),
1 console e audit (`worker_private`), 2 shell (`x86_ps2_shell_task`),
3 mouse (`x86_ps2_mouse_task`). Condividono `X86_PS2_SESSION` e l'oggetto
console. Oggi si alternano solo quando cedono la CPU; l'unico lock è quello
della coda delle richieste alla console (`kernel/console/video/requests`).

| # | Dato | Chi lo usa | Corsa con la preemption |
|---|---|---|---|
| 1 | Coda dei comandi (`kernel/shell/queue.c`) | tastiera scrive, shell legge | Sì. Push e pop leggono lo stato, copiano lo slot e riscrivono lo stato: una sospensione nel mezzo perde o duplica un comando. Il commento in `shell.h` lo dichiara: "no IRQ/SMP access" |
| 2 | Puntatore e stato di disegno della console (`console/runtime.c`) | mouse scrive, console disegna | Sì. `x86_console_mouse` controlla `Service.Busy` e poi aggiorna `Pointer` e `Render.Next`: controllo e azione non sono atomici |
| 3 | Uscita seriale (`kernel_text`, `x86_serial_character`) | tutti i task | Sì per il contenuto: carattere per carattere senza lock, le righe di task diversi si mescolano e i runner, che leggono i marker per riga, fallirebbero a caso. Nessuna corruzione di stato |
| 4 | Contatori (`Keys`, `Bytes`, `Lines`) | tastiera scrive, shell legge | No: una sola scrittura per volta, letture di parole allineate |
| 5 | `Ready`, `Stop`, `Quiet` | IRQ, tastiera, dispatcher | Già gestiti dal protocollo a epoche degli eventi o scritti dal dispatcher, che non viene sospeso |

Le richieste alla console e le operazioni dello scheduler passano già da
lock (`requests/queue.c`, lock del core Rust).

### Proposta

**A. Rendere sicuri subito i task dei driver**: lock sulla coda dei
comandi, lock sull'oggetto console, lock dell'uscita per messaggio e
riscrittura dei messaggi composti da più pezzi. Tocca tastiera, console,
shell e log.

**B. Preemption solo per i task che la ammettono (consigliata).** Ogni task
ha un attributo "sospendibile"; i quattro task della sessione PS/2 non lo
sono e restano cooperativi come oggi. Il timer sospende solo i task
sospendibili (per esempio il calcolo della task 5). È coerente con la
decisione di tenere i driver legati al BSP: un driver diventa sospendibile
quando le corse 1-3 sono risolte per lui.

Decisione dell'utente (14 novembre 2026): **A**, rendere sicuri i task dei
driver prima di attivare la preemption.

Vincoli per A: i lock di `kernel/sync` si prendono in ordine crescente di
livello (oggi 1 coda richieste della console, 200 heap, 300 pagine fisiche,
900 scheduler) e tengono IF=0 per tutta la durata, quindi un task che tiene
un lock non viene sospeso. La coda delle richieste della console ha già il
livello minimo: il lock dell'oggetto console non può avvolgere chiamate a
quella coda senza rivedere i livelli.

### Sotto-task 2c, scelta realizzata (14 novembre 2026)

Le righe composte non sono rese atomiche con un lock né spegnendo gli
interrupt: un lock tenuto per tutta la riga violerebbe l'ordine dei livelli
quando il codice fra un pezzo e l'altro prende altri lock, e gli interrupt
spenti resterebbero spenti su una riga mai chiusa. Il modulo di log tiene
invece traccia della riga aperta (`kernel_log_line_open`); la politica di
preemption della task 3 tratterà una riga aperta come un lock tenuto,
rinviando la sospensione per un numero limitato di tick. Protegge le righe
dalla preemption sul BSP, non da scritture contemporanee di CPU diverse.
