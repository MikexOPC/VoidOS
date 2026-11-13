# Task in attesa e risveglio: contratto v1

8 ottobre 2026. Perimetro: estensione del modulo scheduler esistente, inclusi
core Rust, gateway, dispatcher, test e adapter x86. Scelta eventi con epoca e
wake-all autorizzata dall'utente. Questo documento descrive il contratto e il
piano di prove; i risultati vengono registrati separatamente nel report finale.
[Implementazione ed evidenze](validation.md).
Baseline verificata:
[scheduler cooperativo v0](../../../../docs/validation/consolidamento.md#validazione-dei-backend-x86).

## Ricerca e compromessi

[Linux 6.12, Driver Basics, wait queues](https://www.kernel.org/doc/html/v6.12/driver-api/basics.html),
consultato l'8 ottobre 2026: wait_event ricontrolla la condizione al risveglio;
la pubblicazione della condizione e il wake richiedono un protocollo coordinato.
Non trasferiamo i lock, le barriere o le macro Linux nel nostro BSP cooperativo.
Il riferimento serve a distinguere notifica e condizione, non a copiare codice.

[MIT xv6-riscv, proc.c, sleep/wakeup](https://github.com/mit-pdos/xv6-riscv/blob/riscv/kernel/proc.c),
branch riscv consultato l'8 ottobre 2026: la registrazione dell'attesa e la
pubblicazione del risveglio devono evitare un intervallo non protetto.
Il branch e mutabile, non uno snapshot riproducibile; nessun suo codice e
importato. I lock/SMP e l'ABI RISC-V non appartengono al nostro profilo attuale.

| Alternativa | Semantica | Compromesso |
|---|---|---|
| Evento con epoca, wake-all | Notifica un cambiamento; tutti i waiter ricontrollano la condizione | Semplice, ma non conta risorse disponibili e non memorizza payload |
| Semaforo contatore, wake-one | Un segnale consegna un permesso consumabile a un waiter | Adatto a risorse numerabili; richiede contratto contatore/ownership diverso |

Proposta raccomandata: eventi con epoca. I semafori restano un possibile
primitivo distinto, non una reinterpretazione successiva della stessa API.

## Profilo proposto

- BSP singolo, task kernel fidati, IF=0 durante tutto il carico finito.
- Otto eventi statici, indici 0..7, durata pari al run dello scheduler;
  nessuna creazione/distruzione dinamica e nessun evento identificato da pointer.
- Rust possiede epoche u64 e associazioni di attesa, oltre a coda e stati.
- Aggiungere WAITING senza rinumerare FREE/RESERVED/READY/RUNNING/FINISHED/RETIRED.
- Un waiter conserva stack, lease, pin, contesto e generazione. WAITING non
  autorizza collect, rilascio, riuso dello slot o nuovo ingresso nel task.
- Gli eventi non contengono payload. Il produttore modifica la condizione
  prima di signal; il consumatore la ricontrolla dopo il risveglio.
- Nessuna chiamata FFI dal task attivo: mailbox -> switch dispatcher -> Rust
  -> fine del prestito -> scelta task -> switch. Osservazione dell'epoca e
  signal richiedono anch'essi un passaggio al dispatcher.

## Protocollo contro wake perso

Il token osservato e la coppia indice evento/epoca. Ogni signal incrementa
l'epoca anche quando non esistono waiter. Un wait si blocca soltanto se il token
corrisponde all'epoca corrente. L'epoca non va in wrap: al limite u64::MAX il
signal restituisce errore senza modificare coda, stati o contatori.
Non usare valori futuri arbitrari: un token maggiore dell'epoca e un errore.

Ordine del consumatore: osservare l'epoca tramite dispatcher, ricontrollare la
condizione, quindi eventualmente attendere sul token. La lettura della
condizione non cede il controllo e non richiama codice che possa fare yield.
Nel profilo BSP/IF=0 nessun produttore concorrente IRQ/SMP puo modificarla.
Se un altro task segnala dopo l'osservazione ma prima del wait, l'epoca diversa
impedisce la sospensione. Se segnala dopo la registrazione, il waiter e gia
WAITING e viene accodato READY. Il ritorno da wait non certifica una condizione
vera: il consumatore ripete il controllo.

Pseudocodice di progetto, non codice eseguibile:

```text
task_consumer:
    ripeti entro il limite diagnostico:
        token = observe(event) tramite dispatcher
        se condizione vera: utilizza il dato e termina l'attesa
        wait(event, token) tramite dispatcher
        ricontrolla la condizione

dispatcher_wait(handle, event, observed):
    verifica RUNNING unico, handle, bounds e token non futuro
    transazione Rust unica:
        se observed != epoca[event]: RUNNING -> READY, coda FIFO
        altrimenti: RUNNING -> WAITING, registra event/observed
        current = NONE
    termina la FFI prima dello switch

dispatcher_signal(handle, event):
    verifica RUNNING unico e handle del produttore
    transazione Rust unica:
        incrementa epoca senza wrap
        WAITING sul medesimo evento -> READY, azzera associazione
        accoda waiter in ordine crescente di indice slot
        conserva current RUNNING del produttore
    poi normale YIELD del produttore
    termina la FFI prima dello switch
```

L'ordine crescente e deterministico, non una promessa FIFO tra waiter.
La ready queue mantiene il proprio ordine e i waiter si aggiungono in coda.
Observe e non mutante; il dispatcher usa il normale yield prima di riprendere
il task con il token nella risposta privata. SIGNAL ed eventuale successivo
YIELD sono transazioni distinte: un fallimento dopo SIGNAL e terminale, non
viene presentato come rollback del segnale gia pubblicato.

## Invarianti e failure

Ogni slot WAITING ha una sola associazione valida e non appartiene alla ready
queue; gli altri stati non conservano associazioni di attesa. Nessun WAITING e
current. Nessun READY compare due volte, anche dopo signal ripetuti. Il signal
non fa ripartire direttamente un task e non modifica una generazione.

Il candidato transazionale Rust comprende epoche, associazioni, coda, stati e
contatori; un rifiuto conserva tutto lo stato. Gli accessi sono limitati a otto
slot e otto eventi. Nessun heap e nessun raw pointer nel core. Il layout FFI e
la risposta token/esito richiedono nuovi assert e test C/Rust; l'ABI esistente
non va reinterpretata per sovraccaricare generation con un'epoca evento.

SELECT senza READY deve distinguere completamento da presenza di WAITING:
DONE/EMPTY solo se non esistono waiter; BLOCKED altrimenti. Nel carico finito
BLOCKED senza un produttore eseguibile e un deadlock diagnostico: fail-stop
esplicito, risorse conservate, niente HLT con IF=0 e niente busy loop infinito.
Timeout, cancellazione e teardown di waiter sono rinviati a un contratto
dedicato; non usare collect su WAITING come scorciatoia.

| Rischio | Protezione proposta | Prova richiesta |
|---|---|---|
| Wake prima del wait | Epoca incrementata anche senza waiter, confronto atomico nel core | Tutti gli ordini observe/signal/wait |
| Alias dopo riuso task | Handle indice/generazione, associazione cancellata prima di collect | Vecchio handle respinto senza mutazioni |
| Risveglio duplicato | Solo WAITING puo diventare READY; invarianti membership | Signal ripetuti, coda piena |
| Evento fuori range/epoca futura | Bounds e validazione token | 8, UINT32_MAX, futuro, limite u64 |
| Leak durante sospensione | Stack mantenuto, cleanup solo FINISHED sul dispatcher | Budget invariato durante wait, ripristinato alla fine |
| Deadlock | BLOCKED distinto da DONE, deadline host | Immagine negativa intenzionale |
| Rientro IRQ/NMI | Divieto invariato e gateway esclusivo | Predicato di contesto e reentry fixture |

## Test di accettazione da implementare

Tutti i casi seguenti sono pianificati, non PASS:

1. Rust debug/release e modello indipendente: observe, wait registrato, signal
   prima/dopo wait, evento diverso, sette waiter piu un produttore, otto waiter
   senza produttore (BLOCKED), segnali ripetuti, epoche limite,
   transizioni rifiutate immutabili, generazioni vecchie e BLOCKED vs DONE.
2. Gateway C/Rust con warning come errori: nuovo layout/assert, token distinti
   dagli handle, errori normalizzati, reentry e contesto vietato; ASan fixture.
3. Dispatcher host: mailbox WAIT/OBSERVE/SIGNAL, owner mantenuti durante wait,
   assenza di cleanup anticipato, errori senza salto al contesto sbagliato.
4. QEMU: workload produttore/consumatore con almeno un wait reale e un signal
   anticipato, canary e contesto conservati, conteggi esatti e budget ripristinato.
   Dieci boot consecutivi sull'immagine positiva finale, ESP/hash/CPU/PTE verificati.
5. Negativi isolati: tutti i task WAITING -> marker deadlock atteso; evento
   invalido/token futuro; conservazione risorse e nessun crash inatteso.
   Ripetere guardie/NX/accesso revocato/rollback sul nuovo eseguibile.

Coverage linee/branch N/D finche non misurata; accounting non equivale a
Valgrind. I test QEMU v0 non dimostrano questa nuova estensione ancora assente.

## Struttura e rollout

Core Rust nel modulo scheduler, con un file di responsabilita per eventi/attese
se owner.rs cresce; C resta nella root della propria cartella funzionale.
Test nelle sottocartelle test/state, test/ffi e test/wait; script in scripts.
Documentazione dell'estensione qui, per evitare il sesto file diretto nella
cartella documentation esistente. Sorgenti <=400 righe, cartelle <=5 file.

Ordine: scelta semantica -> core/modello/FFI -> review/debug -> dispatcher e
workload -> review/debug -> gate finali host/QEMU -> rapporto evidenze.
Il kernel main e gia al limite 400: questa estensione deve usare il punto
d'ingresso scheduler esistente, senza aggiungere righe o spostare altro codice.
Il vecchio workload cooperativo resta coperto come regressione.

IRQ attivi richiedono un blocco successivo: il produttore IRQ deve pubblicare
eventi senza entrare nel gateway, con lifetime, ordering, drain e overflow
definiti. IF=0 non e sincronizzazione SMP. Non abilitare STI o aggiungere HLT
per rendere artificialmente funzionante un wait senza produttore.
