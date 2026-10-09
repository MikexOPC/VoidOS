# Coda dati IRQ: progetto v0

2026-10-08. Scelta dell'utente: 256 byte utilizzabili, un produttore IRQ,
un consumer; rifiuto dei nuovi byte con perdita osservabile. Modulo scheduler,
nessun driver input/hardware nuovo, nessuna modifica a reuse/.

## Ricerca e adattamento

[Linux circular buffers](https://docs.kernel.org/core-api/circular-buffers.html)
spiega ownership di head/tail e vincolo produttore/consumer unici.
[seL4 notifications](https://docs.sel4.systems/Tutorials/notifications.html)
separa dati e notifiche coalescenti. Documentazione primaria consultata oggi;
non copiamo implementazioni. La nostra v0 BSP/IF0 usa il guard non bloccante
del registro binding gia esistente, senza promesse di lock-free/SMP.

## Contratto

Queue Rust fissa: head e count bounded, 256 byte, perdita pending e contatore
u32 saturante. Il contatore non dimostra cardinalita degli IRQ. Nessun heap.
Otto channel statici, opzionali, associati agli otto binding/eventi esistenti.
Open richiede token source valido e indice consumer atteso. Il primo pop da
una task verificata fissa la sua generazione; le successive devono coincidere.
Il backend verifica BSP/IF0, task corrente e generazione prima di ogni pop.
Source/token restano l'autorita del produttore. Task fidate, niente ring3.

Pseudocodice NON eseguibile:

```text
push valido:
    se piena o loss pending: rifiuta byte, conta perdita, pubblica evento
    altrimenti: copia byte, incrementa count, pubblica evento
pop consumer:
    se count > 0: restituisci byte FIFO e azzera la cella consumata
    altrimenti se loss pending: restituisci LOSS e sblocca nuovi push
    altrimenti: EMPTY
close:
    sorgente quiescente verificata in C, contesto fine sessione
    niente dati, loss pending o notifiche pending -> reset channel
unbind:
    consentito solo dopo close; non cancella implicitamente dati o perdita
```

La barriera LOSS e ordinata DOPO i dati precedenti alla perdita. Fino al suo
consumo si rifiutano nuovi byte anche se si libera spazio: cosi il decoder
non unisce due frammenti separati da byte persi. Il futuro decoder deve
azzerare il proprio stato parziale quando riceve LOSS. Non viene implementato qui.
Pubblicazione dato/perdita e bit pending avvengono sotto lo stesso guard.
Pop non prende l'Owner e non cambia epoche; prima di dormire il consumer
deve controllare dati/LOSS e usare OBSERVE/ricontrollo/WAIT per evitare wake persi.
La v0 diagnostica registra WAIT prima della consegna one-shot gia verificata.

## Integrazione e accettazione

Il LAPIC gia presente consegna un byte diagnostico (Count modulo 256) tramite
IRQ hardware al consumer 0. Gli altri waiter conservano wake-all ma non possono
consumare dati. Verificare anche token/task generation scaduti e EMPTY finale.
Tre sessioni, riuso event3, close prima di unbind, nessun nuovo registro/Assembly.
Il byte diagnostico non e input PS/2/USB e non dimostra quel device.

Test Rust FIFO, full, wrap degli indici, LOSS ordinato, saturazione, ownership,
reset e modello di riferimento; test ABI C/Rust con ASan C e reentrancy/bounds.
QEMU finale dieci boot consecutivi, regressioni negative e loop, stack/guardie
e budget restituiti. Coverage e hardware fisico N/D, Valgrind non disponibile.
