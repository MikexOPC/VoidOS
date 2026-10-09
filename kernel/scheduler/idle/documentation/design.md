# Dispatcher senza task READY: contratto v0

8 ottobre 2026. Proposta HLT/watchdog approvata dall'utente. Il modulo funzionale
e lo scheduler; le sole modifiche esterne sono i suoi agganci al timer esistente.
Riutilizziamo le interfacce IRQ/LAPIC/HPET, senza nuovi registri o Assembly.

## Fonti e scelta

- [Intel SDM](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html):
  Vol.3A §7.8.1, IF bit9; Vol.2B STI, ritardo fino all'istruzione successiva;
  Vol.2A HLT, ripresa all'istruzione dopo HLT. La sequenza STI/HLT/CLI esistente
  resta nel backend Assembly del watchdog, non viene ricreata in C.
- [Linux 6.12 wait queues](https://origin.kernel.org/doc/html/v6.12/driver-api/basics.html):
  condizione persistente aggiornata prima di wakeup e ricontrollata al risveglio.
  Non copiamo codice/code Linux. Le epoche Rust e la notifica atomica sono quelle
  gia validate dal progetto.

Scegliamo l'attesa supervisionata, anziche polling o HLT privo di deadline.
Il watchdog attuale attende anche acknowledgement del suo one-shot HPET prima
di classificare la source: garantisce quiescenza ma introduce latenza v0.
Non promettiamo risveglio immediato o cancellazione anticipata dell'HPET.

## Invarianti e ownership

BSP cooperativo, task kernel fidate, IF=0 durante stato/switch/mapping. IRQ
nelle task conserva il precedente compute ammesso. Idle avviene sullo stack
principale, current=NONE, dopo un SELECT che distingue BLOCKED da EMPTY.
Nessun borrow Rust vive durante l'attesa hardware e l'handler pubblica soltanto
un bit atomico dopo stop/EOI del driver. La transazione Rust risveglia tutti i
waiter sull'evento; ogni task ricontrolla Count/Phase della source persistente.

La source e READY/consumata, con subscription live, prima della sessione.
Due task acquisiscono stack nuovi, si sospendono sull'evento 3, quindi il
dispatcher arma la seconda scadenza LAPIC e attende con HPET indipendente.
La source e armata solo con coda vuota; IF=0 e condizione Count persistente
escludono la finestra software tra controllo e sonno. Un IRQ hardware pending
viene consegnato dalla sequenza esistente quando IF viene abilitato.

Successo: notifica dopo acknowledgement, drain Rust con due waiter risvegliati,
quattro dispatch effettivi, due raccolte, budget ripristinato/144 PTE assenti.
Il driver consuma poi la stessa seconda consegna e prosegue la terza: il numero
totale di eventi e le ownership driver non cambiano.

Il watchdog conserva il budget originale dell'apertura. Durante l'attesa serve
un prestito contabile distinto: esattamente due lease da sedici pagine, entrambe
identificate nell'allocatore, pinnate e disgiunte, delta libero esattamente 32.
Il watchdog verifica queste identita a ogni gate, senza sottrarre un valore
arbitrario o modificare Budget. Il prestito termina dopo acknowledgement HPET
e wake-all, prima di rilasciare le lease. Poi il gate rigoroso originale resta
invalido fino alla restituzione dei due stack. Close e vietato con prestito vivo;
un timeout conserva il prestito e tutti gli owner nel profilo terminale.
API privata per il dispatcher fidato su BSP/IF=0, non un contratto per syscall,
allocazioni concorrenti/SMP o lease di provenienza non verificata.

Failure: IRQ LAPIC perso, errori source o HPET non disponibile non sono READY.
Se l'attesa non conclude, terminare il profilo con stack/binding validi; niente
collect di WAITING, restart implicito, unbind o rilascio di risorse ancora vive.
Un test isolato ferma il LAPIC prima di HLT: HPET deve riprendere la CPU e
riportare timeout, conservando entrambi gli stack. Entrambe le source perse
insieme restano un limite del modello, non una garanzia misurata.

## Pseudocodice non eseguibile

```text
dispatcher IF=0:
    SELECT
    se EMPTY: sessione conclusa
    se BLOCKED:
        valida current=NONE, stack/lease e binding source
        arma source e attendi con HPET (nessun borrow Owner)
        se fallisce: termina conservando owner/binding
        drain notifiche Rust; verifica READY e waiter risvegliati
        SELECT ancora, senza contare l'attesa come esecuzione task
    esegui task, consuma mailbox e raccogli solo FINISHED
IRQ: salva contesto -> valida source -> stop/EOI -> post atomico -> IRET
```

## Accettazione

Test host ASan per binding, contesto vietato, errori/timeout, notifiche duplicate
e conservazione; Rust per wake-all/batch e pending/rifiuti. QEMU: dieci boot
positivi finali, frame IRQ sullo stack principale durante HLT, IF1, due waiter
risvegliati, CR3/CPU/PTE/accounting finali, source e watchdog chiusi. Negativo
idle-timeout con HPET effettivo e lease trattenute, piu regressioni precedenti
e stop del ciclo timer continuo. Coverage N/D, ASan host distinto da RAM fisica,
niente SMP/hardware reale/preemption/idle generale per driver arbitrari.
