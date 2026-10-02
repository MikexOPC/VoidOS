# Fotografia della mappa di memoria UEFI

## Passo implementato

`memory/map.c` acquisisce la mappa tramite GetMemoryMap, controlla il
formato e gli intervalli fisici e restituisce un BOOT_MEMORY_MAP.
L'ingresso UEFI esegue questa prova dopo quella dei segmenti, libera
il buffer e ritorna al firmware. Non usa ancora la mappa per allocare
RAM, non la passa al kernel e non chiama ExitBootServices.

Il primo GetMemoryMap usa dimensione zero e buffer nullo per ottenere
la dimensione richiesta. Ogni buffer contiene un margine di otto
descrittori, perche AllocatePool puo far crescere la mappa. Se ancora
insufficiente, il modulo libera il buffer precedente e ritenta, al
massimo quattro volte. Il limite del singolo buffer e 1 MiB.
Questi limiti sono scelte del progetto: alcuni firmware possono richiedere
piu spazio o tentativi ed essere rifiutati anche se conformi.

## Proprieta e stato

Il chiamante inizializza BOOT_MEMORY_MAP a zero. Buffer e Capacity
descrivono l'allocazione posseduta; Valid indica se i metadati sono una
fotografia verificata. Il descrittore proprietario non va copiato per
valore, sovrapposto al buffer o modificato mentre una chiamata e attiva.

boot_acquire_memory_map rifiuta un oggetto che possiede gia memoria,
senza sovrascriverlo. Adotta il risultato di AllocatePool solo dopo
EFI_SUCCESS. Anche quando l'acquisizione fallisce, il chiamante deve
chiamare boot_release_memory_map: un buffer puo essere ancora posseduto.

Il rilascio invalida subito fotografia, dimensione e chiave. Azzera
Buffer e Capacity solo quando FreePool riesce. Se fallisce, conserva
la proprieta per un ulteriore tentativo; non dichiara la memoria liberata.
Un rilascio ripetuto dopo successo non richiama FreePool.
L'ingresso corrente segnala un errore terminale di rilascio e ritorna
al firmware: non garantisce il recupero se il firmware rifiuta FreePool.

## Formato e controlli

Il prefisso EFI_MEMORY_DESCRIPTOR x64 e lungo 40 byte. Il modulo usa
DescriptorSize del firmware per avanzare e decodifica i campi numerici
byte per byte, senza cast del buffer a struct o letture disallineate.
Non legge il padding o i campi di eventuali estensioni.

Controlla che la dimensione restituita non superi la capacita, sia
multipla del passo e contenga al massimo 4096 descrittori. Il profilo
attuale accetta versione 1 e passi da 40 a 4096 byte. Controlla basi
fisiche allineate a 4096, numero di pagine positivo, moltiplicazioni e
fine intervallo senza overflow, e assenza di sovrapposizioni. Gli estremi
che richiedono rappresentare 2^64 vengono rifiutati. L'ordine dei
descrittori puo variare: non si assume che il firmware li ordini.
Il controllo delle sovrapposizioni e lineare per le mappe ordinate;
alla prima inversione passa al confronto quadratico, limitato a 4096 voci.
Vedere la [revisione della memoria](review.md) per i controlli del buffer
e le verifiche aggiunte.

La [classificazione conservativa delle regioni](regions.md) legge tipi
e attributi mentre il buffer e vivo. Non li trasforma in una quantita
di "RAM libera" o in un allocatore. La mappa descrive anche regioni
diverse dalla RAM.
Si richiede firmware conforme: il loader non puo impedire al firmware
di scrivere oltre la capacita passata a GetMemoryMap.

## Durata della chiave

MapKey identifica la fotografia al momento della chiamata. Allocazioni,
rilasci e altre operazioni che cambiano la mappa possono invalidarla.
La chiave della prova corrente viene azzerata durante il rilascio e
non va riutilizzata per ExitBootServices. Il percorso finale dovra
acquisire la mappa mentre le risorse persistenti sono ancora allocate.

## Test

`test/memory/map_test.c` verifica passo esteso a 48 byte, crescita,
limiti di tentativi e memoria, versioni non supportate, dimensioni
incoerenti, intervalli sovrapposti, overflow, callback mancante,
allocazione fallita e FreePool fallito durante crescita e rilascio.
Controlla inoltre l'offset ABI GetMemoryMap a 56 byte e l'invalidazione
della chiave. I test integrati e QEMU verificano la nuova fase positiva.

```powershell
./arch/x86/boot/uefi/scripts/build.ps1
./arch/x86/boot/uefi/test/firmware_test.ps1
```

[Specifica UEFI: GetMemoryMap e servizi di memoria](https://uefi.org/specs/UEFI/2.10/07_Services_Boot_Services.html)
