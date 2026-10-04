# Checkpoint: coda COM1 TX limitata

Tre messaggi posseduti da massimo 64 byte vengono conservati in una
coda FIFO statica di 272 byte. Il driver ne verifica la trasmissione
prima di rimuovere e azzerare ciascuno slot. Una quarta submission
restituisce FULL senza sovrascrivere dati o modificare la ricevuta.

## Review e correzioni

- Header e ID sono verificati da Rust con somme checked e indici bounded;
  C ricontrolla gli indici FFI prima delle scritture. Nessun unsafe Rust.
- Next non puo andare in wrap. Complete accetta soltanto l'ID della
  testa. Slot liberi e byte oltre la lunghezza devono essere tutti zero.
- Source, ricevuta, snapshot e queue hanno ownership e intervalli
  espliciti. Operazioni con IF=0; l'handler non accede alla queue.
- Il driver chiama Complete soltanto dopo transmit riuscito: contatore
  confermato dopo EOI, TEMT, IDT e LAPIC inattivo. Una TX parziale fallita
  conserva la testa; nessun retry automatico o falso completamento.
- Corretti collegamento Rust mancante nella DLL di test e attributo
  no_std durante compilazione come modulo della suite Rust.
- Il verificatore firmware controlla anche l'ordine dei tre payload,
  oltre alle occorrenze uniche e agli owner. Prove negative del verificatore
  aggiunte alla campagna: 34 qword corrotte, snapshot incompleto,
  backpressure non provata e payload scambiati, totale 37 rifiuti.

## Evidenza sullo stato attuale

- Suite host passata: 50 test Rust complessivi contati dai log,
  oltre ai gruppi C di regressione. Nuovi test C: 1.000 rotazioni,
  copia privata, FIFO, FULL senza mutazioni, tutti i bit dell'header,
  Reserved, slot sporchi, alias, IF, output preservato e ID esauriti.
- Build senza warning trattati come errori; immagine valida e sette
  mutazioni invalide correttamente rifiutate.
- QEMU 23/23 scenari: kernel/build/firmware-e66b47de8ab94ca3b7fa8bc25b76a923.
  Tre payload ordinati, 134 byte e 140 IRQ per scenario: 3.220 IRQ totali.
  Fotografia finale queue di tutti i 272 byte: Self, Next=4, Completed=3,
  Head=0, Count=0, slot tutti zero. Restano i controlli buffer TX,
  LAPIC persistente, inventario misto, guardie, NX e fault terminali.
- I 23 snapshot sono stati rivalidati dopo l'aggiunta del controllo
  d'ordine; le 37 alterazioni del monitor sono state eseguite separatamente
  su ready-128. L'hook automatico e stato aggiunto dopo l'avvio della
  campagna, quindi non era ancora caricato in quella esecuzione.
- SHA256 ELF: 6C8B4CA6C4A3C605C205E4FEAEE7ABBF5B5A98A4257646CD135E00F434D882F6.
  Hash identico in build, ESP e immagine ready della campagna.
  ELF 110592 byte; segmenti residenti arrotondati a pagina 122880 byte,
  invariati rispetto al checkpoint precedente. Nessuna allocazione dinamica
  o nuovo frame per la queue; costo statico della queue 272 byte.

L'hash riguarda la working tree testata, comprese modifiche utente a
contratto, diagnostici, header ACPI e main, preservate fuori dal commit.
Il modulo rimane UP e parte di una prova finita del bootstrap: niente
servizio permanente, concorrenza SMP, scheduler o RX. Hardware fisico
non testato; il riarmo THRE preliminare resta specifico del modello provato.

## Prossimo blocco

Definire il servizio seriale oltre la prova di bootstrap, separando
inizializzazione, richieste e arresto, prima di introdurre produttori
concorrenti o RX. [Contratto della coda](../../kernel/serial/queue.md).
