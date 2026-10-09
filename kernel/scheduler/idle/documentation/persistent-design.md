# Task persistenti: contratto proposto v0

8 ottobre 2026. Stato: design scelto dall'utente, primo rollout verificato;
arresto al checkpoint dopo lavoro corrente e cleanup, senza drenaggio imposto.
Modulo: scheduler; nessun nuovo driver, registro o backend Assembly.
Segue la [review dei linguaggi](language-review.md).

## Riferimenti e scelta

Linux 6.12 documenta kthread_stop/kthread_should_stop: richiesta di arresto,
risveglio e cooperazione del thread, con attenzione alla durata di task_struct.
Le wait queue verificano nuovamente la condizione dopo il risveglio.
[Fonte primaria, Driver Basics 6.12](https://docs.kernel.org/6.12/driver-api/basics.html).
Sono riferimenti di contratto, non codice da copiare; il nostro dispatcher e
BSP cooperativo e non acquisisce automaticamente le garanzie di Linux.

Proposta: stop_requested persistente per handle, appartenente all'Owner Rust.
La richiesta non libera stack e non trasforma direttamente la task in Finished.
L'handler IRQ continua a pubblicare solo notifiche; l'arresto viene richiesto
dal dispatcher con IF=0, nessuna task RUNNING e nessun borrow su uno switch.

Alternativa di policy da scegliere: arresto al primo checkpoint prima di nuovo
lavoro, oppure drenaggio bounded del lavoro gia accettato prima dell'uscita.
Il primo non impone al core una coda payload inesistente; il secondo richiede
un contratto esplicito del consumer, capacita, deadline e comportamento overflow.
Nessuna delle opzioni consente di distruggere una task mentre esegue cleanup.

## Stati e ownership

| Stato alla richiesta | Transizione proposta |
| --- | --- |
| READY | Resta READY, flag impostato; nessuna seconda entry in coda |
| WAITING | Token rimosso, READY e accodamento una sola volta, flag impostato |
| RUNNING presente nell'Owner | Rifiuto nel profilo dispatcher v0; nessuna mutazione |
| FREE/RESERVED/RETIRED | Rifiuto; ABORT delle risorse riservate resta distinto |
| FINISHED | Rifiuto; percorso di raccolta esistente |

Stop ripetuto su READY con flag gia impostato: successo idempotente senza nuovo
accodamento. Handle vecchio non modifica la task nuova. Flag azzerato solo nel
rilascio/reinizializzazione verificati; nessuna associazione per solo indice.
Le transazioni Rust conservano rollback, checked counters e validazione Owner.

WAIT dopo una richiesta valida non deve parcheggiare di nuovo la task:
produce un esito STOP_REQUESTED distinto da CHANGED e WAITING e restituisce
il controllo alla task tramite READY, per eseguire cleanup e EXIT esplicito.
Prima di WAIT si validano comunque handle, evento e token; un argomento invalido
non viene mascherato dall'arresto. QUERY_STOP non cambia stato ed e verificata
dal gateway prima di ogni accesso Owner. ABI nuova definita dopo la scelta.

Segnale e stop vengono serializzati nel dispatcher: signal poi stop non
duplica READY; stop poi signal non riattacca token o annulla il flag. Il flag
non consuma un evento condiviso e non falsifica la generazione dell'evento.
Event pending resta una notifica coalescente, non un conteggio di payload.

C mantiene lease/pin/mapping fino a EXIT e ritorno sullo stack dispatcher.
La raccolta libera risorse native e poi COLLECT secondo il contratto esistente;
un fallimento terminale conserva ownership e non viene dichiarato cleanup.
Quiescenza e unbind di un futuro driver richiedono il suo contratto specifico.

## Pseudocodice non eseguibile

```text
task:
    ripeti:
        osserva generazione evento
        se stop richiesto: cleanup e EXIT
        ricontrolla condizione persistente
        se lavoro disponibile: elabora un blocco bounded; checkpoint
        altrimenti: WAIT con generazione osservata
        al ritorno ricontrolla stop e condizione

dispatcher:
    senza RUNNING, applica richieste stop e drain notifiche
    SELECT READY; valida contesto/lease; switch senza borrow Rust
    al ritorno applica WAIT/YIELD/EXIT
    su BLOCKED invoca backend idle supervisionato
    su EXIT raccoglie solo dal proprio stack
```

L'ordine osserva-generazione/ricontrolla-condizione/WAIT evita che un segnale
fra verifica e sospensione venga interpretato come assenza di lavoro. Il
consumer deve ricontrollare la propria condizione anche dopo un wake-all.
Un thread che non raggiunge checkpoint non e arrestabile forzatamente dal
profilo cooperativo. Una deadline rileva il mancato progresso, non libera stack.

## Implementazione prevista e gate

1. Rust: modulo stop separato, flag e invarianti bounded, comandi a handle;
   test di ordini stop/signal/wait, errori e rollback, esaurimento contatori.
2. Gateway C ABI: layout verificati e risultati nominati; test C/Rust con
   contesto mock, invalidi e vecchie generazioni. Nessun raw pointer al core.
3. Integrazione C: separare driver del dispatcher dai profili diagnostici;
   workload finito di attese ripetute e arresto con marker distinti.

Accettazione: cicli ripetuti su piu eventi senza wake perso, nessun doppio READY,
stop di WAITING che permette cleanup reale della task, vecchi profili invariati,
budget/pin/PTE ripristinati dopo uscita, dieci boot consecutivi sull'immagine
finale, regressioni negative, warning gate e test host/sanitizer applicabili.
Il workload deve definire numero di cicli e deadline prima di eseguirsi; non
confondere una prova finita con compatibilita generale dei driver.

Il primo rollout aggiunge core/FFI stop e una sessione diagnostica con 32
attese ripetute su due eventi, stop di un consumer sospeso e raccolta nativa.
La sessione viene eseguita dopo il profilo idle gia esistente, senza ulteriori
IRQ o registri. Non generalizza ancora l'idle hardware a sorgenti arbitrarie
o numero arbitrario di stack: quel contratto resta un passo successivo.
Il codice delle fault injection e separato dal controller in test/fault/.
Gate eseguiti e limiti sono nel [report finale](persistent-validation.md).
