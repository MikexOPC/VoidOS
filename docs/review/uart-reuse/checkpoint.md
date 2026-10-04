# Checkpoint: riuso esplicito TX e tre invii consecutivi

Lo stesso buffer COM1 trasmette 44, 64 e 26 byte, con rilascio verificato
fra gli invii. Count rimane cumulativo; il ciclo non alloca nuova memoria.
Non introduce ancora una coda o produttori concorrenti.

## Implementazione e review

- Release richiede IF=0, progressione coerente con le consegne confermate,
  Done, THRE/TEMT e IIR senza cause pendenti. Il chiamante verifica anche
  EOI, ISR/IRR vuoti e distacca l'owner IRQ prima dell'azzeramento.
- Tutti gli 88 byte vengono azzerati dopo le verifiche. Un rifiuto conserva
  i dati precedenti; un invio parziale non viene resettato automaticamente.
- TxBase fotografa Count per ogni invio. Rust controlla il goal
  Base+Length+1 prima di aprire IER, impedendo wrap del contatore.
- Owner IRQ 48 byte, con prefisso ABI Assembly invariato. TxCompleted
  e totale byte aumentano soltanto dopo completamento, Drain e LAPIC idle.
- Un EOI finale guasto puo lasciare Done=1, ma Count non conferma la
  chiusura: Release rifiuta il buffer. Non viene attestato un successo falso.
- Mantenuti handler da un byte per IRQ, mapping LAPIC persistente,
  inventario misto e chiusura verificata della sorgente alla fine.

## Evidenza

- Suite host finale passata, 48 test Rust complessivi contati dai log.
- 256 cicli alternati 64->1, con readback di tutto il buffer azzerato,
  doppio rilascio rifiutato e contatore cumulativo.
- Rifiuti di Release per contatore errato, IF=1, UART attiva, DLAB,
  timeout TEMT o IIR non conforme, senza mutazione dei dati.
- Modello integrato di due invii con basi diverse e di EOI finale guasto.
- Immagine valida e sette mutazioni invalide verificate.
- 23/23 scenari QEMU: kernel/build/firmware-1c14120a6dba4204b03ec7286e66aa8b.
  134 byte e 140 IRQ per scenario, totale 3220 IRQ. Tre payload unici sul
  canale seriale, TxBase=113, TxCompleted=3 e tutti gli 88 byte finali
  verificati dal monitor. La coda del buffer corto non contiene residui.
  Coda sporca e payload intermedio alterato su copie dei log rifiutati.
- SHA256 ELF: C430B8E4B63A6F464E5358C2C114033A6EAB555D3A164BD47039030A06006BDC.
  ELF 110592 byte, segmenti residenti arrotondati a pagina 122880 byte:
  crescita di una pagina rispetto al checkpoint precedente. Nessun nuovo
  frame o buffer dinamico per il riuso. Hash identico in build, ESP e QEMU.

L'hash e della working tree testata. Modifiche utente a istruzioni,
contratto, diagnostici, header ACPI e main preservate fuori dal commit.
Hardware fisico non testato; profilo UP anche nella configurazione
QEMU a quattro CPU. Nessun claim di concorrenza o portabilita universale
del riarmo THRE preliminare.

## Prossimo blocco

Una coda TX bounded con backpressure e policy esplicita di completamento.
Il percorso di riuso e ora verificato, ma il bootstrap resta una prova
delimitata; scheduler, IRQ permanenti e RX richiedono moduli successivi.
[Contratto del modulo e fonte TI](../../kernel/serial/transmit.md).
