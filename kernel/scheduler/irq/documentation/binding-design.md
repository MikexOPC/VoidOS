# IRQ-eventi: contratto di durata v0

Scelta dell'utente: una sorgente per evento, token di generazione; piu task
possono attendere lo stesso evento. Profilo BSP, IF=0 durante modifica e
callback ISR, niente NMI/SMP. Nessuna modifica a registri o Assembly.

## Ricerca e adattamento

[Linux v6.17 manage.c](https://github.com/torvalds/linux/blob/v6.17/kernel/irq/manage.c)
e [generic IRQ, free_irq/disable_irq](https://docs.kernel.org/core-api/genericirq.html)
distinguono stop della periferica, rimozione dell'handler e completamento dei
gestori. Il nostro token software non certifica da solo quiescenza hardware.
[seL4 notifications](https://docs.sel4.systems/Tutorials/notifications.html)
illustra notifiche coalescenti separate dai dati: conserviamo questo principio,
non importiamo codice o capability architecture. Documentazione web consultata
2026-10-08; Linux v6.17 e riferimento fissato, non versione corrente dichiarata.

## Stati e ownership

Otto slot associati agli otto eventi. Ciascuno contiene identita u64 della
sorgente fidata e generazione u64. Nessun puntatore viene dereferenziato in Rust.
Una sorgente non viene registrata contemporaneamente su due slot. Generazioni
non azzerate al distacco; saturazione rifiuta il riuso senza wrap. Registro
protetto da guard atomico non bloccante; nessun borrow oltre una chiamata FFI.
ISR non accede all'Owner scheduler e non chiama callback/log/heap.

Pseudocodice NON eseguibile:

```text
bind: contesto BSP/IF0/dispatcher o fine sessione, evento senza pending
      slot libero, source non zero/unica, incrementa generazione -> token
post: source e token corrispondono -> pubblica bit; altrimenti rifiuta
drain: dispatcher applica wake-all, rifiuto conserva pending
detach: contesto valido, source hardware quiescente verificata in C
        nessun pending sullo slot -> azzera solo source, conserva generazione
```

Il distacco NON cancella notifiche, NON ferma il device e NON libera stack.
Il chiamante ferma/acknowledge la source prima, consuma notifiche e conclude la
sessione. Nel backend LAPIC si riusano owner validator, Active/Phase/Error e
x86_lapic_idle; nessun nuovo offset. Fallimento trattiene token/source/risorse.
Generazioni binding e task ed epoche evento sono contatori distinti.

## Integrazione e prove previste

I tre IRQ LAPIC gia presenti usano token espliciti: worker event2, idle event3,
riuso idle event3 con nuova generazione. La vecchia API event-only rimane solo
in build host esplicitamente configurate per le regressioni precedenti; non
viene esportata nell'immagine kernel. Una nuova fixture FFI usa API bound reali.

Test: duplicati, source estranea, token scaduto, pending al distacco, overflow,
contesto negato/reentrancy, saturazione/coalescing, riuso e conservazione di
pending su rifiuto. Dieci boot finali consecutivi; regressioni fault esistenti,
due sessioni idle, ISR task/HLT e accounting stack. File <=400, cartelle <=5.
Niente dichiarazioni di SMP, payload queue, driver input o hardware fisico.
