# Design: scheduler preemptive nell'immagine di produzione

Stato: 3d e 3e fatte per la sessione PS/2 sul BSP. L'obiettivo 1 di S1
resta parziale: manca il dispatcher permanente (B2). Il design riprende
le scelte di [K4-C](../../kernel/scheduler/documentation/design.md)
e di [immagine unica](immagine-unica.md).

## Obiettivo

Nell'immagine di produzione lo scheduler non termina mai, sospende i task
con il timer su tutte le CPU avviate e non ha limiti fissi minuscoli sul
numero di task.

## Cosa esiste oggi

| Pezzo | Dove | In produzione |
|---|---|---|
| Core Rust: stati, coda globale sotto lock, `current` per CPU, affinità | `kernel/scheduler/rust/` | Sì, usato dal BSP |
| Preemption all'uscita dell'IRQ verso il dispatcher | `arch/x86/kernel/scheduler/`, `arch/x86/kernel/interrupts/return/` | Attiva nella sessione PS/2 sul BSP, anche fuori dalla finestra di attesa |
| Dispatcher sugli AP, task concorrenti, preemption AP | `arch/x86/kernel/scheduler/batch/`, `arch/x86/kernel/scheduler/admission/` | Solo diagnostici `KERNEL_TEST_AP_*`. L'ordinaria offre il job fidato `ap <n>` su AP1 |
| Timer LAPIC periodico | `arch/x86/kernel/scheduler/irq/preempt.c` | D1h sul BSP durante la sessione PS/2, quanto nominale di 10 ms |
| Modello di esecuzione | Dispatcher a sessioni | `exit` chiude la sessione e arresta il BSP. Senza PS/2 il BSP attende in `KERNEL_IDLE` con interrupt attivi |
| Capacità | 8 task: coda, registro IRQ e canali Rust da 8; stack in 8 posizioni da 18 pagine nella tabella delle finestre (voci 320-463) | 8 |
| Driver | console, PS/2, shell, watchdog presuppongono il BSP | BSP |

La [validazione della 3e](../validation/consolidamento.md#cpu-e-scheduler)
riporta i gate del runner per la shell e il soak nell'immagine ordinaria.
`meson-bsp-periodic`, `ap-task-four` (otto task su quattro AP) e
`ap-task-concurrent-stress` sono prove diagnostiche.

## Prossimo passo di S1

B2 rende permanente il dispatcher sul BSP nell'immagine ordinaria. Il
dispatcher deve restare operativo senza PS/2 e dopo la chiusura della
shell, con gli interrupt attivi durante l'idle. Il design di B2 deve
definire il ciclo di vita di shell e servizi, il significato di `exit`
e come verificare il rilascio delle risorse quando il dispatcher rimane
attivo.

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

### Task 2, risultato (14 novembre 2026): fatta

- 2a: la coda dei comandi della shell passa da un lock di livello 10.
- 2b: lo stato del puntatore della console passa da un lock di livello 3.
  Un evento del mouse che arriva durante il disegno viene accumulato e
  applicato dopo il disegno, invece di fermare il kernel. Sotto il lock
  girano solo funzioni senza lock; la coda delle richieste (livello 1) si
  usa fuori dal lock.
- 2c: riga di log aperta tracciata, vedi sopra.

Prove sull'immagine `ordinary` Meson ricompilata da `a947255`:

- Suite host ps2, ps2-mouse, console, pointer, x86-framebuffer e log; il
  test della console fallisce con il vecchio comportamento.
- Campagne `k6-ordinary` 10/10, `k6-shell` 10/10, `k6-no-ps2` 10/10,
  `k6-shell-bridge-disabled` 3/3, `k6-shell-bridge-fault` 3/3.
- Campagne del mouse e della console: plain 3/3, v2c 10/10, k3-soak 1/1,
  fallback 2/2, absent 2/2, stall 5/5, `paging-tamper`.
- `--ordinary-soak 30`: 30 minuti, 308 giri di `bridge status`, 309
  comandi contati dal kernel, budget dello scheduler invariato, contatori
  di errore del watchdog a zero.

Limiti: le corse sono chiuse per la preemption sul BSP; nessun lock
protegge da accessi di CPU diverse, che oggi non esistono perché i driver
restano sul BSP. La preemption non è ancora attiva: la prova vera arriva
con la task 3. Le campagne del mouse richiedono Pillow nel Python usato
dai runner. Hardware fisico non provato.

### Task 3, sotto-task 3a e 3b (15 novembre 2026)

- 3a: il vettore D1h, già usato dagli AP per il timer, ha un posto
  riservato nel registro IRQ del BSP (`X86_IRQ_TIMER_SLOT`). I quattro
  posti dei dispositivi 40h-43h restano invariati.
- 3b: la politica è una funzione pura nel codice comune
  (`kernel/scheduler/preempt.c`). A ogni tick: ignora se non gira un task o
  se il timeslice non è finito; rinvia se c'è un lock tenuto; rinvia al
  massimo 4 tick se la riga di log è aperta, poi sospende e conta uno
  sforamento; altrimenti sospende. Il budget della riga riparte a ogni
  cambio di task. I contatori saturano.
- Il collegamento al ritorno dall'IRQ passa dalla 3b alla 3c: senza timer
  nessun tick lo eseguirebbe, quindi non sarebbe provato.

### Task 3, sotto-task 3c (15 novembre 2026)

Timer LAPIC periodico da 10 ms sul vettore D1h durante la sessione della
shell (`arch/x86/kernel/scheduler/irq/preempt.c`).

- Si arma in `x86_ps2_activate`, dopo i bind dei dispositivi, e si ferma
  all'inizio della chiusura della sessione, prima dei controlli di
  quiescenza. Alla chiusura stampa i contatori `KERNEL_PREEMPT_*`.
- Conflitto risolto con la scelta A dell'utente: il servizio residente in
  ring 3 usa lo stesso timer LAPIC (one-shot su 44h). Intorno a ogni
  richiesta `bridge status` il timer di preemption viene fermato, il tick
  pendente svuotato e poi riarmato. Debito: un solo timer condiviso fra
  kernel e processi (alternativa B) nell'obiettivo processi.
- Il core Rust si modifica solo dal dispatcher, quindi l'IRQ non chiama
  `tick_on`. L'IRQ decide solo se è sicuro sospendere (politica 3b, con
  timeslice di un tick sempre finito) e consegna il comando `X86_TASK_TICK`.
  Il dispatcher chiama `tick_on`: se un altro task è pronto applica PREEMPT,
  altrimenti YIELD e lo stesso task riparte.
- Fino alla 3d i task girano con gli interrupt spenti fuori dalla finestra
  di attesa: i tick sospendono solo task in attesa. È preemption vera ma
  limitata.
- Errore commesso e corretto: la prima versione chiamava `tick_on`
  dall'IRQ, il core rifiutava e il kernel si fermava senza messaggio
  (`kernel_scheduler_fail_stop`). Resta il difetto noto che quell'arresto
  non stampa il motivo.
- Secondo errore, trovato dalla campagna `k6-shell` (fallimento al boot 7
  su 10, riprodotto al primo boot della ripetizione). Per far consegnare un
  D1h rimasto pendente, l'arresto del timer apriva una breve finestra IF=1.
  Lì entrava anche l'IRQ della tastiera, che pubblicava l'evento 4 dopo
  l'ultimo svuotamento del dispatcher. `xcn_close` svegliava poi il worker
  e la chiusura falliva (`SCHEDULER_DISPATCH_FAILURE: 6`). Correzione:
  durante quella finestra CR8 vale 4. I vettori 40h-4Fh restano in IRR e
  passa solo D1h (classe Dh), secondo SDM Vol. 3A, 11.8.3.1 e 11.8.6. Il
  difetto dipende dai tempi e non esiste un test deterministico; la prova
  sono le ripetizioni di boot.
- Difetto esterno trovato dalla sessione da 30 minuti (3 corse fallite su
  8). Con QEMU 11.1 (TCG), l'HPET a volte supera il comparatore senza
  generare il match: i registri letti al fallimento mostrano stato zero,
  interrupt abilitato, contatore 2,56 s oltre l'obiettivo e voce IOAPIC
  libera. Prima della 3c questo evento probabilmente causava un fermo di
  circa 43 s (non verificato). Con i tick D1 il budget di 256 risvegli si
  esauriva e il kernel si fermava. Decisione dell'utente: opzione B, cioè
  il watchdog rileva il match saltato e lo recupera
  (`arch/x86/kernel/timer/hpet/watchdog/documentation/design.md`, punto
  6). L'opzione A, fermare D1 durante l'idle, avrebbe solo nascosto il
  difetto.

Prove sull'immagine `ordinary` Meson con la 3c completa:

- Suite host watchdog, kernel, scheduler, x86-scheduler, ps2, ps2-mouse,
  console e build-plan. Controprove: senza il ramo YIELD il test del motore
  fallisce; tre mutazioni del recupero HPET fanno fallire i casi 1501, 1511
  e 1607.
- Campagne 13/13 (72 boot): `k6-ordinary`, `k6-shell`, `k6-no-ps2`,
  `k6-shell-bridge-disabled`, `k6-shell-bridge-fault`, `k6-process`,
  plain, v2c, k3-soak, fallback, absent, stall, `paging-tamper`. Prima
  della correzione CR8, invece, `k6-shell` falliva 1 boot su 8; dopo, 30/30.
- `--ordinary-soak 30`, tre QEMU in parallelo: 11 sessioni su 12 verdi
  (308-311 giri di `bridge status`, sforamenti 0). In una il recupero HPET
  è scattato e la sessione è proseguita pulita (`KERNEL_WATCHDOG_MISSED: 1`).
- Dubbio aperto, accettato dall'utente: in 1 sessione su 21 complessive,
  dopo la chiusura della shell, un AP non ha confermato lo shootdown del TLB
  entro 1 s (`K4TLBACK`). Causa ignota, ipotesi non verificata il carico di
  tre QEMU con log `-d int`. Nove sessioni con diagnostica sullo shootdown
  non lo hanno riprodotto. Si ricontrolla nelle campagne lunghe della 3e.
- Non provato su hardware fisico.

### 3d-1 — IF durante le transizioni dello scheduler

- `kernel_preempt_section_enter/leave` salva e ripristina solo IF, usando
  il backend IRQ dei lock. L'ingresso annidato salva IF=0 e lascia gli
  interrupt spenti fino all'uscita dalla sezione esterna.
- Yield e richieste di evento disabilitano gli interrupt prima di leggere
  lo stato del dispatcher. Il task ripristina IF dopo la ripresa e la
  verifica del contesto, dopo aver acquisito anche la risposta per gli
  eventi. L'uscita del task consegna il controllo al dispatcher a IF=0.
- La routine di attesa conserva IF sullo stack del task. Apre la finestra
  IRQ dopo aver salvato il contesto e ripristina IF dopo averlo ricostruito.
  La sospensione da IRQ continua a ripristinare i flag tramite il ritorno
  dall'interrupt. Riferimento: Intel SDM Vol. 3A, §7.8.1.
- Al termine della 3d-1 i task ordinari partono a IF=0. Sono successivi
  l'estensione del predicato IRQ, la verifica dei dati condivisi e
  l'avvio dei task con IF=1.

### 3d-2 — Ammissione del frame IRQ di un task BSP

Nell'immagine ordinaria `x86_ps2_irq_record` usa il task BSP corrente.
Accetta i frame con IF=1 e DF=0 per i vettori già supportati. Il RIP deve
puntare a una pagina di codice del kernel eseguibile in supervisore.
L'RSP deve stare nello stack del task corrente, con una pagina di margine
sopra la base mappata. La guard page è sotto quella base. Il predicato non
dipende più dall'indice 1 né dagli indirizzi della finestra di attesa.

I profili diagnostici del timer fuori dalla sessione PS/2 usano i loro
predicati per finestra. I task ordinari partono ancora a IF=0.

### 3d-3 — Dati condivisi con gli IRQ

- Tastiera e mouse tolgono i payload dalle code con IF=0. Il controllo
  di coda vuota e l'azzeramento del segnale `Ready` avvengono nella
  stessa sezione. Il consumatore tastiera registra e scarta la coda
  del guasto prima di riaprire gli IRQ. Osservazione dell'epoca, seconda
  lettura della coda e attesa preservano il protocollo di risveglio.
- Il worker fotografa `Ready` e `Quiet` prima di decidere se attendere.
  La shell fotografa i contatori prima di eseguire il comando. La
  validazione del task BSP e la pubblicazione del lavoro AP rispettano
  il requisito IF=0 delle verifiche native. La seriale pubblica lo
  stato di riga aperta prima del primo carattere, per il rinvio
  limitato della preemption durante la scrittura.
- I lock delle code comandi, delle richieste console e del puntatore
  proteggono già le rispettive mutazioni. Il bridge pubblica la
  richiesta dalla shell e la consuma nel dispatcher, che sospende
  il tick durante il passaggio a ring 3. Gli handler non scrivono
  lo stato della richiesta.
- Il worker della console mantiene una sola sezione a IF=0 per
  ciascun gruppo di disegno, inclusi preparazione e verifica delle
  lease. Ripristina IF prima di passare al gruppo successivo. Il
  limite del backend di disegno è otto celle per gruppo.
- L'audit periodico della console usa lo stesso scanner e i suoi
  controlli di autorità, topologia, contabilità e alias. Lo scanner
  e il controllo degli alias aprono un punto di ripresa ogni 128
  voci. A quel punto il worker ripristina IF, cede il BSP e riprende
  a IF=0 solo dopo aver ricontrollato le generazioni di paginazione
  e allocatore. L'inventario atteso rimane privato sullo stack del
  worker. Se cambia una generazione, l'audit scarta la scansione e
  la riavvia alla successiva occasione di inattività. Una violazione
  a generazione stabile resta un errore terminale.

### 3d-4 — Preemption fuori dalla finestra di attesa

- Nel percorso BSP che combina la sessione PS/2 e i processi utente,
  il bootstrap verifica task, stack ed entry point a IF=0 e abilita
  gli IRQ prima di chiamare il task. Yield, eventi e uscita usano
  le sezioni critiche della 3d-1. La sospensione dal timer ripristina
  IF con il ritorno dal frame IRQ.
- `KERNEL_PREEMPT_OUTSIDE_WINDOW` conta le sospensioni quando il
  RIP salvato dal timer è fuori dall'intervallo della finestra di
  attesa. `KERNEL_PREEMPT_SUSPENDED` include entrambi i percorsi.
  Il runner applica i gate ai contatori nelle sessioni concluse della 3e.
- La console riporta in `KERNEL_PAGING_AUDIT_MASKED_MAX_PM` il
  massimo campionato dal PM timer nelle singole porzioni di audit
  a IF=0. Il tempo totale dell'audit include anche i cambi di task.
- I task degli AP e i profili PS/2 senza processi utente conservano
  l'avvio a IF=0. La prova di preemption riguarda il BSP.

### 3e — Chiusura delle prove BSP

Il runner delle sessioni shell e del soak richiede i tre contatori
`KERNEL_PREEMPT_OUTSIDE_WINDOW`, `KERNEL_PREEMPT_SUSPENDED` e
`KERNEL_PREEMPT_OVERRUNS`. Fallisce per dati mancanti, nessuna sospensione
fuori finestra, sospensioni fuori finestra maggiori del totale o
sforamenti diversi da zero. Conserva i numeri nel risultato JSON.

Il carico reale è la sessione ordinaria con input, rendering della
console e richieste al servizio ring 3 attraverso `bridge status`.
Il conteggio viene raccolto dal RIP del frame IRQ del timer.

Il flag `KERNEL_TEST_SCHEDULER_PREEMPTION` e il caso `bsp-preemption`
sono ritirati. I due pianificatori rifiutano `-TestSchedulerPreemption`
con un messaggio esplicito. Il diagnostico periodico usa il proprio flag
esistente e resta solo diagnostico. La sua prova richiede il marker
periodico, che certifica le sei sospensioni e l'arresto del timer.

Il boot che si ferma alla shell per verificare le protezioni e il caso
senza PS/2 mantengono i rispettivi criteri. Il gate sui contatori viene
applicato dopo la chiusura di una sessione di task BSP.
