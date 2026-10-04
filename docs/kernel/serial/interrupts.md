# COM1: prima prova di IRQ hardware con ritorno

Il modulo `arch/x86/kernel/serial/interrupts/` esercita tre eventi THRE
del modello UART 16550A di QEMU, poi trasmette 134 byte in tre invii usando un
buffer TX limitato. La console diagnostica continua a usare polling.
E una prova delimitata
della catena UART -> I/O APIC -> LAPIC -> IDT -> handler -> IRETQ.
L'handler viene registrato nel [registro IRQ persistente](../interrupts/registry.md)
prima di aprire la sorgente e rimosso solo dopo la chiusura verificata.

## Rotta e stato

COM1 usa la porta legacy 0x3f8 e IRQ ISA 4. La MADT puo sostituire GSI,
polarita e trigger; il piano Rust rifiuta flag riservati. Il piano C
richiede una sola copertura I/O APIC, capacita coerenti e nessuna
collisione con altri IRQ ISA, inclusi quelli impliciti, o NMI globali.
Il vettore assegnato e 0x40 e la destinazione fisica e il BSP.

Le capacita, il profilo, la finestra e il suo allocator sono owner
kernel persistenti gia verificati dal preflight APIC. Gli helper di
applicazione rotta ed EOI sono interni al kernel: non accettano indirizzi
arbitrari da programmi esterni. La rotta viene mascherata prima di
cambiarne destinazione e riletta dopo l'apertura. Il selettore IOREGSEL
viene salvato e ripristinato. Un errore impedisce il successo; non si
riaprono deliberatamente le rotte firmware.

Lo stato statico occupa 48 byte, con Count/Error ai primi due offset
u32 verificati dal contratto ABI. La rotta occupa 24 byte, oltre al
flag di esecuzione singola. Il puntatore TX segue il prefisso precedente
di 32 byte, seguito da TxBase e TxCompleted. Il buffer TX posseduto
occupa 88 byte, con 64 byte di dati.
Non vengono allocati frame o buffer dinamici.

## Finestra con IF=1 e completamento

LCR deve essere 3, quindi DLAB=0 e formato 8N1. Il trasmettitore deve
essere vuoto, senza errori LSR. MCR OUT2 viene aperto, con readback;
IER abilita soltanto THRE. Ogni arm parte con ISR/IRR vuoti e IF=0.

L'Assembly abilita IF soltanto durante una polling loop limitata a
10 milioni di iterazioni, che legge Count/Error. Non e una scadenza
in millisecondi. Ogni uscita successiva a STI esegue CLI. Il foreground
non usa allocazioni, log o finestra RAM mentre IF=1.

L'IRQ gate azzera IF, salva GPR/FXSAVE64 ed esegue CLD. Il frame 0x40
richiede IF=1 salvato, CS=8, SS=16 e stack principale persistente.
Nelle tre consegne preliminari l'handler legge IIR e accetta soltanto
THRE con FIFO disabilitata o
funzionante (0x02/0xc2); rifiuta bit riservati o stato FIFO guasto.
Chiude IER con readback
e verifica che IIR non indichi interrupt pendenti. Poi controlla che
ISR contenga esclusivamente il vettore 0x40, scrive zero in EOI LAPIC
e verifica ISR vuoto. Count aumenta soltanto dopo tutti questi passi.
Errori dell'handler sono terminali, senza attestare una consegna riuscita.

Il LAPIC usa ora una pagina dedicata, UC, supervisor, RW e NX,
presente per l'intera vita della prova: l'handler non modifica piu
la finestra RAM. Setup e chiusura I/O APIC continuano a usare prestiti
temporanei con IF=0. NMI/fault restano terminali. Il foreground della
prova continua a leggere soltanto i contatori mentre IF=1.
Il contratto resta UP, senza nesting o SMP.

Dopo tre consegne, IER e chiuso e la prova iniziale e verificata.
Prima della chiusura finale segue la
[trasmissione TX delimitata](transmit.md): tre invii da 44, 64 e 26 byte,
con un IRQ di chiusura per invio e riuso esplicito del buffer. Il contatore
finale vale 140, inclusi i tre
IRQ iniziali. Poi OUT2 torna al valore iniziale, la
rotta e mascherata, Active=0, IF=0, ISR/IRR vuoti, IDTR invariata e
finestra temporanea revocata. Il mapping LAPIC resta presente, con owner
valido e inventario RAM/MMIO ricatturato e verificato. Non richiede nuovi
frame RAM. `KERNEL_UART_IRQ_OK` segue queste verifiche.
[Ownership e durata del mapping dedicato](../../mm/devices/lapic.md).

## Test e limiti

Host: default/override ISA, collisioni, NMI, bounds e capacita; rotta
con un guasto per ogni accesso controller; ACK/EOI ordinati, IIR inatteso,
IER non disabilitabile, EOI ignorato, ISR assente e guasti nelle letture
ISR prima/dopo EOI. Rust verifica tutti i 256 flag/cause testati.

QEMU: traccia indipendente di esattamente 140 `Servicing hardware
INT=0x40`, snapshot in memoria Count=140/Error=0/Active=0, profilo
persistente, gate e controller finali chiusi, PTE LAPIC UC presente e
inventario misto confrontato con la memoria reale; regressione di 23 scenari.

Il riarmo THRE con IER mentre THRE e gia alto e esplicito nel sorgente
QEMU, che segnala che non e specificato nel datasheet. Il test non
dimostra la portabilita di questo riarmo a tutte le UART fisiche.
Su un modello che richiede una transizione TX reale puo fallire per
timeout. TX invia ora dati reali, ma rimane una prova finita, senza
produttori concorrenti.
La [coda TX limitata](queue.md) conserva ora tre messaggi posseduti;
il driver la svuota in ordine dopo ogni completamento verificato.
Non sono ancora implementati RX a IRQ, RX FIFO, discovery PCI/ACPI della
UART, SMP, AVX/XSAVE o ritorno a user mode.
Hardware fisico non testato.

## Fonti verificate

- [TI PC16550D, datasheet originale distribuito da DigiKey](https://media.digikey.com/pdf/Data%20Sheets/Texas%20Instruments%20PDFs/PC16550D.pdf):
  IER, IIR/THRE, LCR/DLAB, MCR e LSR; lettura IIR come ACK THRE.
- [QEMU, implementazione UART 16550A](https://raw.githubusercontent.com/qemu/qemu/master/hw/char/serial.c):
  riarmo IER.THRI con LSR.THRE gia alto e cancellazione THRE leggendo IIR.
- [Intel SDM Vol. 3A](https://www.intel.com/content/dam/www/public/us/en/documents/manuals/64-ia-32-architectures-software-developer-vol-3a-part-1-manual.pdf):
  ISR/IRR, EOI, interrupt gate e ritorno IA-32e.

Per ripetere: `kernel/test/host.ps1`, `kernel/scripts/build.ps1 -Install`,
`kernel/test/image.ps1` e `kernel/test/firmware.ps1 -UseInstalled`.
