# Checkpoint: sonno della CPU in attesa di un evento IRQ

Validazione del 4 ottobre 2026. Il blocco introduce una primitiva
di attesa su BSP e la esercita con il timer LAPIC gia calibrato.

## Cambiamento e revisione

Il timer non usa piu il ciclo PAUSE per le tre scadenze. La nuova
primitiva controlla Count/Error con IF=0, esegue STI/HLT consecutivi
e CLI al ritorno, poi ricontrolla lo stato. Un evento gia consegnato
ritorna immediatamente. Budget 1–1024 limita i risvegli; il driver usa 8.
Rust verifica risultato, budget e consegna esatta. Il timer registra
i tre conteggi di sonno e le tre prove di ritorno immediato.

Il prestito della source dura tutta l'attesa; il chiamante garantisce
memoria kernel mappata e valida. Non vengono create allocazioni o
subscription aggiuntive. Il gestore IRQ esistente conserva anche RCX,
RDX, R8 e R9 usati dal ciclo Assembly. Il cleanup del timer resta
quiescente e torna con IF=0. L'owner cresce da 120 a 136 byte.

Prima di proporre il codice sono state corrette due incompatibilita
con MASM: visibilita della label tra procedure e forma dell'indirizzo
usato da LEA. La campagna finale ricompila tutte le varianti.

## Risultati sullo stato finale

- Test host C e 57 test Rust superati; warning come errori.
- Assembly reale eseguito su host solo per rifiutare IF=1 prima
  di accedere alla source o eseguire istruzioni privilegiate.
- QEMU: 23 scenari, 128/256 MiB e configurazione smp4, con soli BSP
  attivo; invarianti precedenti di UART, memoria, NX e fault ancora verdi.
- Un HLT per ciascuna delle tre scadenze. RIP salvato dalle tre
  consegne hardware coincide con l'istruzione dopo HLT, con IF=1.
- Byte STI/HLT/CLI confermati dalla memoria dell'immagine eseguita.
- Tre ritorni senza HLT sull'evento gia osservato; prove ring zero
  di source nulla/disallineata, errore, Goal zero e budget invalidi.
- 13 alterazioni respinte dal verificatore timer, inclusi opcode HLT
  rimosso e falso indirizzo di ritorno. Restano verdi le 37 alterazioni
  TX e le 16 del registro IRQ.
- Immagine valida e 7 mutazioni invalide verificate.

Log: `kernel/build/sleep-host-final.log`, `sleep-firmware-final.log`,
`sleep-monitor.log`, `sleep-image.log` e
`firmware-37fd1cc91ac042f7bb685940b17fec67/`.
Le due ulteriori alterazioni degli opcode sono state provate separatamente
sulla stessa fotografia ready-128 dopo la campagna completa.
ELF verificato e installato nella ESP: 126.976 byte, SHA256
`E2CB7332D68F3362A779174450B93323DC97DB6D78F1AE0FD9A915B2F1BF7449`.

## Limiti e prossimo blocco

HLT non garantisce timeout se manca ogni interrupt di risveglio.
La campagna ha un timeout esterno, ma il kernel non ha ancora un
watchdog indipendente. Non e dimostrato il comportamento su hardware
fisico o con stati energetici che arrestano il LAPIC timer senza ARAT.
Il budget esaurito dopo risvegli estranei non viene dichiarato testato
con hardware reale; il controllo del risultato e verificato in Rust.

Questo non e ancora un event loop permanente o uno scheduler:
la prova chiude le sorgenti e termina con IF=0. Il prossimo blocco
deve definire driver persistenti, consumo degli eventi e arresto,
mantenendo una politica esplicita per i timeout.

Contratto e riferimento Intel: [attesa eventi](../../kernel/interrupts/event-wait.md).
