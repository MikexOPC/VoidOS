# COM1: trasmissione reale con buffer posseduto

`arch/x86/kernel/serial/transmit/` gestisce una trasmissione finita di
1..64 byte. Il buffer e copiato prima di aprire IER; non conserva un
puntatore ai dati del chiamante. Self rende l'owner non trasferibile.
Rust valida Length, Next e Done prima degli accessi C. Non ci sono
allocazioni, code dinamiche o unsafe Rust.

## Stati e interrupt

L'owner occupa 88 byte: Self u64, Length/Next/Done/Reserved u32 e Data[64].
Prepare richiede IF=0, owner vergine e sorgente/output disgiunti, fuori
da MMIO, finestra RAM e vista ricorsiva. Una preparazione fallita non
pubblica una copia parziale. Non si riusa implicitamente un owner completato.
Il chiamante kernel garantisce un owner scrivibile e una sorgente leggibile
per la lunghezza indicata: questa API interna non valida puntatori user mode.

Start verifica LCR=3, DLAB=0, IER=0 e THRE/TEMT prima di abilitare solo
IER.THRE. Nel profilo attuale OUT2 e rotta sono gia aperti. Il primo IRQ
avvia i dati; quelli successivi derivano dal reale svuotamento del THR/FIFO.
Il riarmo della prova preliminare a trasmettitore vuoto resta un limite
di portabilita distinto, gia documentato.

Ogni IRQ valida la causa THRE, FIFO funzionante/disabilitata, LCR, IER,
LSR e coerenza fra consegne e indice. Scrive al massimo un byte nel THR
e incrementa Next. Non effettua polling, allocazioni o log nell'handler.
Non rilegge IIR dopo la scrittura: una nuova causa puo gia essere pendente.

Dopo l'ultimo byte, un ulteriore IRQ chiude IER con readback e verifica
assenza di cause pendenti, poi imposta Done. EOI richiede ISR 0x40 e
readback ISR vuoto. Count aumenta soltanto dopo EOI riuscito. Un EOI
fallito tenta anche la chiusura IER e termina senza confermare l'evento.
Un byte gia scritto non puo essere annullato: un guasto puo lasciare
una trasmissione parziale, mai attestata come riuscita.

Il foreground attende Count con il loop Assembly bounded esistente.
IF torna zero su ogni uscita da STI. Dopo l'ultimo IRQ, Drain attende
THRE e TEMT per un massimo di 100000 letture, senza scrivere dati:
buffer consumato non equivale ancora a registro di shift svuotato.
Sono budget di iterazioni, non scadenze in millisecondi.

## Prova e controllo indipendente

Il bootstrap conserva la prova preliminare di tre IRQ e invia poi il
payload di 44 byte `CR LF KERNEL_UART_TX_PAYLOAD: 0123456789abcdef CR LF`.
Il totale e 48 IRQ: tre iniziali, 44 scritture e un IRQ di chiusura.
Il successo segue Drain, chiusura rotta, OUT2 ripristinato, ISR/IRR vuoti,
IDTR invariata, finestra libera e inventario misto verificato.

QEMU controlla il payload sul canale seriale, una sola occorrenza,
contatore e traccia IRQ, copia in memoria di tutti gli 88 byte e puntatore
TX nel nuovo owner IRQ di 40 byte. Il mapping LAPIC resta valido.
Host verifica tutte le lunghezze, copia indipendente, progressione,
overflow, alias, metadati corrotti, cause inattese, DLAB, errori LSR,
IER bloccato e timeout TEMT. Un test integrato collega TX, EOI e contatore.

Il contratto e UP: nessun produttore modifica dati durante IF=1 e
nessun log polling compete con il trasferimento. Non e ancora un driver
seriale permanente: niente RX, ring buffer concorrente, batching FIFO,
scheduler, SMP o API user mode. Hardware fisico non testato.

[TI PC16550D, THRE/TEMT e gestione degli interrupt](https://media.digikey.com/pdf/Data%20Sheets/Texas%20Instruments%20PDFs/PC16550D.pdf).
[QEMU, modello UART e nuova causa durante la scrittura THR](https://raw.githubusercontent.com/qemu/qemu/master/hw/char/serial.c).
