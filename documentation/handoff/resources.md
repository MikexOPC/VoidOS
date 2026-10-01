# Stack dedicato e copertura delle risorse nel firmware

## Blocco implementato

Il loader ora alloca uno stack dedicato, mantiene vivi kernel e stack
durante la lettura della mappa e verifica che tutti i loro intervalli
siano descritti dal firmware come EfiLoaderData. Prima del ritorno
libera il buffer mappa, lo stack dedicato e le pagine del kernel.
L'entry ELF sintetica continua a non essere eseguita.

## Stack

`handoff/stack.c` usa AllocatePages(AllocateAnyPages, EfiLoaderData)
per 16 pagine contigue: 64 KiB. Controlla base non nulla, allineamento
a 4096 e somma senza overflow prima di scrivere. Azzera esattamente
le pagine allocate. Top e l'estremo superiore esclusivo, allineato
anche a 16 byte: non e un valore RSP gia configurato per una chiamata.
Il limite di 64 KiB e una scelta iniziale da misurare col futuro kernel.

BOOT_KERNEL_STACK e un proprietario, da inizializzare a zero e non
copiare per valore. Non puo essere riutilizzato mentre possiede pagine.
AllocatePages fallito non trasferisce alcuna proprieta, indipendentemente
dal contenuto dell'output. Un output riuscito ma invalido viene adottato
per poter tentare FreePages; non si scrive nell'indirizzo invalido.
Il rilascio invalida subito Top. Se FreePages fallisce conserva base
e numero di pagine per un successivo tentativo senza doppio rilascio.

Il loader non cambia RSP e continua a usare lo stack fornito da UEFI.
Non sono ancora presenti guard page o protezioni MMU/NX dello stack.
L'allineamento numerico non completa il contratto ABI del kernel:
serviranno una convenzione di chiamata definita e il codice di transizione.

## Copertura e coerenza

`memory/ownership.c` verifica che un intervallo sia coperto interamente
da descrittori EfiLoaderData senza attributo RUNTIME. Puo attraversare
piu descrittori adiacenti, anche quando il firmware li restituisce
fuori ordine. Un buco, un tipo diverso o una regione runtime fallisce.
La funzione lavora su una fotografia viva, immutabile e gia verificata;
non alloca o dereferenzia la memoria fisica che sta descrivendo.

`handoff/resources.c` rivalida prima la mappa completa e poi verifica:

- firma, versione e dimensione del contratto dei segmenti;
- corrispondenza delle pagine possedute con la descrizione;
- dimensioni, somme, allineamenti e intervalli senza overflow;
- assenza di sovrapposizioni tra segmenti fisici e stack;
- copertura di ogni segmento e dello stack come EfiLoaderData.

Il controllo non sostituisce il parser ELF, non dimostra autenticita
del kernel e non prova i permessi MMU effettivi. Gli attributi firmware
descrivono anche capacita: non si deducono impostazioni cache o RW/NX.
I tipi runtime restano esclusi anche se qualcuno li riporta insieme
al tipo loader. Il proprietario resta necessario: una mappa di tipo
loader, da sola, non dimostra chi abbia allocato una regione.

Le risorse non cambiano durante la fotografia controllata. Il rilascio
successivo invalida la chiave, quindi questa mappa non va riutilizzata
per ExitBootServices. I controlli sono limitati dal profilo esistente
di 4096 descrittori e 16 segmenti, senza una nuova copia della mappa.

## Memoria ed errori

Lo stack aggiunge 64 KiB, allocati dopo il rilascio del buffer file.
Il controllo della copertura non alloca memoria. La fotografia usa
fino a 1 MiB. Con la fixture corrente: 8 KiB di segmenti, 64 KiB di
stack e il buffer mappa, oltre agli overhead del firmware.

Gli errori di allocazione, verifica e stampa portano alla pulizia.
Un errore di rilascio non impedisce il tentativo sulle altre risorse.
Il primo errore dell'operazione viene preservato. Se il firmware
rifiuta il rilascio, il loader restituisce errore: non puo garantire
il recupero delle pagine dopo un fallimento persistente di FreePages.

## Test e prova

I test host controllano azzeramento e sentinelle oltre i 64 KiB,
output invalidi dell'allocatore, fallimento e successivo rilascio riuscito,
descrittori disallineati e non ordinati, copertura spezzata, buchi,
tipi sconosciuti o errati, runtime, overlap, overflow e contratto incoerente.
I mock storage sono separati in kernel_memory_mock.c per mantenere
i file ordinati e sotto il limite di dimensione del progetto.

I test integrati simulano stack non allocabile, copertura errata e
FreePages dello stack fallito, verificando la pulizia del kernel.
QEMU verifica la sequenza reale: stack, mappa, copertura e rilascio.

```powershell
./arch/x86/boot/uefi/scripts/build.ps1
./arch/x86/boot/uefi/test/firmware_test.ps1
```

## Fonti

- [UEFI: AllocatePages, tipi di memoria e mappa](https://uefi.org/specs/UEFI/2.10/07_Services_Boot_Services.html)
- [UEFI: ambiente di esecuzione x64](https://uefi.org/specs/UEFI/2.10/02_Overview.html)
- [Microsoft: uso dello stack x64](https://learn.microsoft.com/en-us/cpp/build/stack-usage?view=msvc-170)
