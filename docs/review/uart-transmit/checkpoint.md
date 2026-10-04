# Checkpoint: COM1 TX reale tramite interrupt

Il driver conserva la prova preliminare di tre IRQ e trasmette un
payload reale di 44 byte attraverso un buffer posseduto di 64 byte.
Ogni consegna scrive al massimo un byte; il completamento richiede
la chiusura di IER, EOI verificato e svuotamento THRE/TEMT.

## Implementazione e review

- Modulo funzionale transmit con C e validazione scalare Rust dei bounds.
  Buffer non trasferibile di 88 byte; owner IRQ esteso da 32 a 40 byte,
  con prefisso Count/Error e ABI Assembly conservati.
- Prepare copia dati espliciti e pubblica solo dopo verifica di dimensioni,
  ownership e disgiunzione; nessuna allocazione o puntatore sorgente live.
- Start verifica UART vuota, LCR e IER; il foreground non usa log polling
  mentre IF=1. Handler senza attese, log o allocazioni, un byte per IRQ.
- Indice coerente con Count. L'ultima consegna chiude IER; il foreground
  attende TEMT prima di dichiarare successo. Budget bounded, senza claim
  di durata in millisecondi.
- EOI fallito tenta anche la chiusura IER e non incrementa Count.
  Un byte gia scritto puo comunque essere uscito: gli errori non promettono
  rollback dei dati sul canale seriale.
- IIR finale accetta solo 0x01/0xc1: letture assenti o riservate non
  attestano una sorgente chiusa. Il mapping LAPIC e inventario misto
  restano verificati dopo la chiusura della rotta.
- La prima build ha generato un riferimento implicito a memcpy: la copia
  e stata resa esplicita per il link freestanding. Corretto anche il path
  del test Rust e ripristinato lo stato del mock dopo la prova EOI guasto.

## Evidenza

- Suite host kernel passata, con 47 test Rust complessivi contati dai log.
- Tutte le lunghezze 1..64 e 2080 bytes trasmessi nel modello di buffer;
  copia privata, metadati, overflow, alias e indirizzi esclusi verificati.
- Cause UART, DLAB, LSR, IER bloccato, IIR finale invalido e timeout TEMT.
  Test integrato di TX -> EOI -> Count, incluso EOI ignorato senza successo.
- Immagine valida e sette mutazioni invalide verificate.
- Campagna finale: kernel/build/firmware-48877154aecf421fa080f28fe6c16dda.
  23/23 scenari passati: 48 IRQ per scenario, payload seriale unico e copia
  degli 88 byte verificata dal monitor indipendente. Verificati anche
  NMI, double fault, NX, guardie e stack. Payload e owner alterati su
  copie dei log vengono rifiutati dal verificatore.
- SHA256 ELF: 3B42AA1BEE47AB542AB9DDF661867202D2CE6AF540BEEB08AE9EFCDF361E02A1.
  ELF 106496 byte, segmenti residenti arrotondati a pagina 118784 byte:
  nessuna crescita di pagine residenti rispetto al checkpoint precedente.
  Hash identico per build, ESP installata e immagine ready-128 provata.

L'hash identifica la working tree testata. Le modifiche utente a main,
contratto, diagnostici, header ACPI e istruzioni restano fuori dal commit.
Hardware fisico non testato. La prova iniziale conserva il riarmo THRE
gia alto verificato sul modello QEMU; non e una garanzia per ogni UART.

## Feedback e prossimo blocco

Ora esiste un trasferimento TX osservabile, con ownership semplice e
budget dell'handler chiaro. Questo e un buon primo percorso driver,
ancora delimitato: niente RX, produttori concorrenti, batching FIFO,
scheduler o servizio seriale permanente. Un byte per IRQ privilegia
la verifica; non pretende throughput ottimale.

Prima di un servizio permanente, servono riuso esplicito del buffer,
coda e gestione del completamento; RX richiede una propria policy per
overflow e gestione degli errori. [Contratto e fonti](../../kernel/serial/transmit.md).
