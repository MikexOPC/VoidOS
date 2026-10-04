# Checkpoint: ciclo di vita degli handler IRQ

Il dispatcher non chiama piu COM1 direttamente: passa da un registro
persistente di 120 byte. Il registro gestisce registrazione, consegna
e rimozione verificata. 0x40 serve COM1; 0x41 e riservato al futuro timer,
ma nessuna sorgente timer viene aperta in questo checkpoint.

## Review

- ID monotoni e token correnti impediscono rimozioni tramite token
  scaduti. Nessuna sostituzione implicita di un handler vivo.
- Bind richiede sorgente quiescente; COM1 apre la rotta solo dopo Bind.
  Unbind richiede chiusura confermata di IER, rotta e LAPIC inattivo.
- Busy impedisce rientro durante Handle e Quiet; non costituisce un
  lock SMP. Mutazioni soltanto sul BSP con IF=0.
- Delivered aumenta solo dopo successo del driver, incluso EOI.
  Un fallimento rende la subscription faulted e arresta il dispatch,
  senza tentare EOI generici o conteggiare successi falsi.
- Record e token sono azzerati solo dopo quiescenza. Se Unbind fallisce,
  il cleanup non libera il mapping LAPIC sotto un handler ancora registrato.
- Rust valida vettori hardware, fault e generazioni; il profilo frame
  ammette 0x41 con IF salvato attivo, ma una subscription assente resta
  un errore terminale. Self-test e spurious restano separati.
- Le callback hanno lifetime kernel e sono riferimenti fidati. I controlli
  degli indirizzi non sono una barriera per codice utente o driver ostili.

## Evidenza

- Suite host finale verde, 52 test Rust complessivi contati dai log.
  Test C: due subscription, 1.000 cicli, token scaduti/alterati,
  IF, alias, callback fuori intervallo, rientro in Handle e Quiet,
  quiescenza negata, fault persistente e saturazione dei contatori.
- Build senza warning, immagine valida e sette mutazioni invalide rifiutate.
- QEMU 23/23 scenari: kernel/build/firmware-314b0350c94a485bb1a52f94a8409ddf.
  Ogni scenario conferma 140 IRQ COM1 nel registro e nella traccia hardware,
  134 byte TX ordinati e rimozione finale. Totale: 3.220 IRQ.
  Monitor confronta tutti i 120 byte del registro, incluso Next=2,
  Busy=0 e slot azzerati. Conservati i controlli queue TX, guardie,
  NX, inventario misto, LAPIC persistente e fault terminali.
- 16 alterazioni del verificatore registro rifiutate su ready-128,
  eseguite separatamente dopo la campagna. Il relativo hook e stato
  aggiunto dopo l'avvio della campagna. Le 37 prove negative queue
  gia integrate sono passate nella campagna.
- SHA256 ELF: E53CA1BE8EFFDECFBEF8A4C3100567D5CDEED61CBD733A930066E3E5A1202882.
  Immagine 114688 byte; segmenti residenti arrotondati a pagina 126976 byte,
  crescita di una pagina rispetto al checkpoint queue. Registro 120 byte,
  token COM1 16 byte; nessuna nuova allocazione dinamica per il modulo.

L'immagine e della working tree testata: modifiche utente a contratto,
diagnostici, header ACPI e main preservate fuori dal commit.
Il registro dura quanto il kernel; il funzionamento rimane la prova finita
del bootstrap. Nessun event loop permanente, timer attivo, scheduler,
SMP operativo o hardware fisico verificato.

## Prossimo blocco

Timer LAPIC: selezione delle capacita e della sorgente di calibrazione,
poi prima consegna a interrupt verificata. Una frequenza reale deve essere
misurata o ottenuta da capacita documentate, mai assunta da QEMU.
[Contratto e fonte Intel](../../kernel/interrupts/registry.md).
