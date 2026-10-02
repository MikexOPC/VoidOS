# Risorse persistenti dei descrittori

Il boot prepara GDT/TSS/IDT, contesto, stack e codice in pagine possedute.
La build diagnostica le verifica e libera; `build.ps1 -BootKernel` le
conserva e usa il [percorso di attivazione](../transition/activation.md).

## Proprieta e disposizione

`BOOT_DESCRIPTOR_STORAGE` possiede undici pagine contigue EfiLoaderData
piu una pagina distinta EfiLoaderCode: 48 KiB. Il chiamante possiede
l'owner; `BOOT_EXIT_HANDOFF.Descriptors` lo prende in prestito.
Nel nuovo grafo gli intervalli sono in identita.

| Risorsa | Offset fisico dalla base dati | Pagine | Permessi |
| --- | --- | --- | --- |
| Descrittori di 704 byte, contesto a offset 1024 | 0 | 1 | RW/NX |
| Guardia RSP0 | 4096 | 1 posseduta | Non mappata |
| Stack RSP0 | 8192 | 4 | RW/NX |
| Guardia IST1 | 24576 | 1 posseduta | Non mappata |
| Stack IST1 | 28672 | 4 | RW/NX |
| Codice | Allocazione CodeBase distinta | 1 | R/X |

Le guardie consumano RAM ma non hanno foglie nel nuovo grafo. Prima del
cambio CR3 permessi e guardie dipendono dal paging firmware. Non vengono
creati alias scrivibili del codice nel nuovo grafo.

## Allocazione e pubblicazione

L'allocatore verifica preflight, allineamento, limite fisico, profilo
canonico basso e alias con owner, metadati, risorse e firmware prima di
adottare ogni allocazione. Una risposta firmware ambigua non viene
adottata, azzerata o liberata: potrebbe appartenere a un altro proprietario.
Non si puo garantire recupero attraverso firmware non conforme.

Prima di copiare il codice aggiorna GetMemoryMap e verifica copertura
EfiLoaderCode senza runtime, XP, RP o RO. Ready viene pubblicato dopo
verifica di codice e blocco. Questi controlli presuppongono un firmware
che rispetti le proprie mappature.

Il mapper aggiunge tre intervalli RW/NX e una pagina R/X a una bozza;
la pubblica solo dopo verifica. Duplicati, alias e foglie nelle guardie
sono respinti senza cambiare il piano originale. DescriptorBase protegge
anche le guardie da alias delle tabelle; CapsuleBase identifica il codice.

Il preparatore ricostruisce il grafo inattivo, aggiorna fotografia e
contratto v2. Il preflight terminale ricontrolla grafo, blocco, codice,
copertura EfiLoaderData/EfiLoaderCode e guardie a ciascun tentativo.

## Rilascio e costi

Il rilascio revoca Ready e firma, poi libera prima il codice. Se fallisce
conserva entrambe le allocazioni. Se fallisce il successivo rilascio dati
conserva soltanto quell'owner. Ogni base viene cancellata dopo successo;
un owner vuoto e idempotente. Il cleanup richiede firmware disponibile
e descrittori inattivi: invalidare una firma non scarica GDTR, IDTR o TR.

Nel percorso diagnostico un errore permanente di FreePages puo lasciare
memoria allocata dopo il ritorno EFI, quando l'owner locale non e conservato.
Dopo ExitBootServices questo cleanup non viene chiamato.
Le allocazioni consumano 48 KiB; le tabelle aggiuntive dipendono dai percorsi
gia presenti. Non e una misura del picco totale del bootloader.

## Verifiche

La build verifica alias, corruzione, rollback, mapping e rilascio
ritentabile delle due allocazioni, oltre a diciannove scenari dell'ingresso
EFI con guasti di allocazione, fotografia, attributi e profilo CPU.
Il [test kernel QEMU](../transition/activation.md) verifica l'utilizzo reale
dopo uscita UEFI e cambio CR3. Hardware fisico e NMI annidati restano esclusi.

Riferimenti: [servizi UEFI](https://uefi.org/specs/UEFI/2.11/07_Services_Boot_Services.html)
e [Intel SDM 3A](https://cdrdv2-public.intel.com/825758/253668-sdm-vol-3a.pdf).
