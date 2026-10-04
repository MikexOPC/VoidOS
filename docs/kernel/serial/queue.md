# COM1: coda TX limitata con completamento esplicito

Il modulo `arch/x86/kernel/serial/queue/` conserva tre messaggi da
1..64 byte. Occupa 272 byte, senza heap: header di 32 byte e tre slot
di 80 byte. Copia i dati alla submission; non conserva puntatori alla
sorgente e non sovrascrive messaggi presenti quando la coda e piena.

## Contratto e proprieta

Initialize accetta soltanto un owner vergine. Self impedisce che una
copia della struttura venga usata come nuovo owner. Next parte da uno;
Completed indica l'ultimo messaggio rimosso. Rust verifica Head < 3,
Count <= 3 e Next = Completed + Count + 1 senza overflow. C controlla
ID consecutivi, lunghezza, Reserved, coda del payload e slot inattivi.
Il validatore Rust usa soltanto scalari, senza unsafe o dereference FFI.

Submit restituisce INVALID, OK, FULL oppure EXHAUSTED. FULL conserva
la coda e la ricevuta del chiamante: quest'ultimo decide quando riprovare.
Gli ID non vengono riutilizzati tramite wrap. Source, ricevuta e owner
devono avere intervalli disgiunti; ricevuta e strutture sono allineate
a otto byte. I controlli escludono overflow, MMIO LAPIC, finestra RAM e
vista ricorsiva. Il chiamante garantisce memoria kernel mappata e valida:
questa API interna non costituisce validazione di puntatori user mode.

Front restituisce una copia posseduta senza rimuovere la testa. Complete
accetta soltanto l'ID della testa e azzera tutti gli 80 byte del suo slot.
Il chiamante deve avere verificato TX, ACK, EOI, TEMT e LAPIC inattivo.
Complete non consulta l'hardware: questa responsabilita rimane al driver.
Un errore dopo una trasmissione parziale conserva il messaggio; non avvia
un retry automatico che potrebbe duplicare byte sul canale seriale.

Le operazioni richiedono IF=0 sul BSP. Validate non modifica lo stato,
ma richiede comunque una fotografia stabile. La coda non viene letta
dall'handler IRQ. Non e una coda concorrente SMP o NMI-safe.

## Integrazione e verifiche

Il bootstrap riempie i tre slot con payload da 44, 64 e 26 byte. Una quarta
submission viene rifiutata, poi il driver invia in ordine tre copie usando
lo stesso buffer TX da 88 byte. Ogni slot viene rimosso soltanto dopo
il ritorno positivo di transmit, che verifica contatore IRQ, Drain,
IDT e LAPIC inattivo. Totale: 134 byte e 140 IRQ, inclusa la prova iniziale.
Alla fine Next=4, Completed=3, Head=0, Count=0 e tutti gli slot sono zero.

Host verifica copia privata, FIFO, coda piena senza mutazioni, 1.000
rotazioni, alias, IF, output preservato, tutti i bit dell'header e dei
Reserved, residui negli slot e ID vicini all'esaurimento. QEMU confronta
payload, traccia IRQ e tutti i 272 byte finali dal monitor, oltre ai
controlli del buffer TX e della mappatura persistente LAPIC.
Il verificatore rifiuta anche 37 alterazioni dei log e del monitor,
inclusi ordine dei payload scambiato, slot sporchi e snapshot incompleti.

La coda serve attualmente la prova finita del bootstrap. Sono ancora
da progettare il servizio permanente, scheduler, produttori concorrenti,
RX e discovery UART. Hardware fisico non testato; il riarmo THRE iniziale
rimane verificato sul modello QEMU.

[Trasmissione e contratto hardware](transmit.md).
[TI PC16550D: THRE, TEMT e IER](https://media.digikey.com/pdf/Data%20Sheets/Texas%20Instruments%20PDFs/PC16550D.pdf).
