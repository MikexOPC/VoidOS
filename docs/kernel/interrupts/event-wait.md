# Attesa di un evento IRQ senza polling

`x86_irq_sleep` prende in prestito il prefisso Count/Error di una source
kernel viva, allineata a 8 byte. Il chiamante garantisce memoria valida
per tutta l'attesa, stack e IDT pronti, ring zero, IF=0 e una sorgente
armata capace di svegliare il BSP. Il driver timer verifica queste
condizioni prima di invocare la primitiva.

La primitiva controlla Error e Count con IF=0. Se Count ha gia raggiunto
Goal, ritorna immediatamente. Altrimenti esegue STI e HLT consecutivi,
poi CLI al risveglio e ripete il controllo. Questo chiude la finestra
tra osservazione del contatore e sonno per gli interrupt mascherabili:
STI ritarda la loro riconoscibilita fino alla fine dell'istruzione
successiva. Il gestore torna mediante IRETQ; il prefisso viene sempre
riletto dopo il risveglio, quindi un risveglio estraneo non vale come
consegna dell'evento richiesto.

Il contatore deve essere monotono durante il prestito e non andare in
wrap. La primitiva generica accetta Count >= Goal; il driver timer
richiede inoltre Count == Goal, per rifiutare consegne inattese.

Il risultato e zero su errore oppure 1 piu il numero di HLT eseguiti.
Il budget ammesso e 1–1024 risvegli. Goal zero, source nulla o
disallineata, budget invalido ed Error nonzero vengono rifiutati.
Con IF gia uno, la funzione rifiuta senza modificarlo e senza leggere
la source. Su tutti i percorsi ammessi di ring zero ritorna con IF=0.
Non modifica il contatore, non alloca e non conserva il prestito.

## Verifica e limiti

Il timer esercita tre cicli arm/sonno/IRQ/consumo con budget 8. Registra
un sonno per scadenza e tre prove di ritorno immediato sull'evento gia
consegnato. Rust valida la consegna esatta e il numero di risvegli.
QEMU confronta i tre RIP salvati dalla consegna hardware con l'indirizzo
subito dopo HLT, IF=1 nella traccia e i byte STI/HLT/CLI nell'immagine.
Sono esercitati anche i rifiuti con IF=0; host esegue il codice Assembly
reale soltanto sui percorsi che rifiutano IF=1, senza istruzioni
privilegiate. Non si pretende di emulare HLT con un mock host.

Il budget limita i risvegli, non il tempo trascorso dentro HLT. Una
sorgente guasta puo impedire il ritorno: watchdog e sorgente temporale
indipendente non sono ancora implementati. La campagna QEMU rileva
questo caso attraverso il proprio timeout. Inoltre il timer APIC puo
fermarsi in certi stati di risparmio energetico se ARAT non e presente;
non viene dichiarato supporto generale all'idle su hardware fisico.

Questo blocco e una primitiva di attesa verificata, non un event loop
permanente: dopo la prova finita sorgenti e subscription sono chiuse.
Restano da introdurre durata permanente dei driver, eventi consumabili,
politica di timeout e solo successivamente scheduling. Non e un lock
SMP o un'interfaccia chiamabile da user mode.

[Intel SDM, STI e HLT](https://cdrdv2-public.intel.com/782151/253667-sdm-vol-2b.pdf)
definisce il ritardo degli IRQ dopo STI e il ritorno dal sonno.
