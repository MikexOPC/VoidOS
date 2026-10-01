# Buffer persistente della mappa di memoria

`memory/final/storage.*` possiede pagine contigue `EfiLoaderData` tramite
`BOOT_FINAL_MEMORY_MAP`. Il campo `Snapshot` contiene una vista della
mappa nel buffer, mentre `Pages` identifica l'allocazione da rilasciare.
La vista non e un secondo proprietario: non va passata a
`boot_release_memory_map`, che usa `FreePool`.

## Allocazione e limiti

Una query iniziale ottiene dimensione e stride. Il buffer comprende un
margine di otto descrittori ed e arrotondato a pagine da 4 KiB. Il limite
rimane 1 MiB; non viene allocato l'intero limite a ogni avvio.
Dimensionamento e arrotondamento vengono controllati prima di AllocatePages.
La stessa [libreria Rust numerica](../libraries/buffer-capacity.md) calcola
le capacita del buffer temporaneo e di quello persistente.

Prima dell'allocazione vengono verificati piano, larghezza fisica e
proprietario delle tabelle. Prima di scrivere si confronta l'indirizzo
restituito con segmenti, stack, contratto, tabelle e metadati vivi.
Si usa la [libreria degli intervalli](../libraries/address-ranges.md).

Un risultato che si sovrappone a risorse vive viene rifiutato senza
scriverlo, adottarlo o liberarlo: una risposta firmware ambigua non deve
creare due proprietari dello stesso intervallo. Un risultato distinto
ma disallineato, nullo o fuori dal profilo fisico rimane registrato per
tentare il rilascio, senza essere dereferenziato.

## Aggiornamento e rilascio

`boot_refresh_final_map` aggiorna nello stesso buffer, senza allocazioni,
e verifica che tutte le sue pagine siano coperte da `EfiLoaderData` senza
attributo runtime. Una crescita oltre capacita o un'altra anomalia
invalida la fotografia e conserva l'allocazione. Non ridimensiona il
buffer automaticamente.

`boot_release_final_map` invalida prima i metadati e usa soltanto FreePages.
Un rilascio fallito conserva buffer, capacita e numero di pagine, quindi
puo essere ritentato. Il successo azzera il proprietario e un nuovo
rilascio non richiama il firmware. Un errore di cleanup viene riportato
dal boot e impedisce il messaggio di rilascio completo delle risorse.

Il buffer resta vivo fino al cleanup del percorso di prova. In una futura
uscita riuscita da UEFI dovra restare riservato al kernel; il rilascio
tramite servizi firmware sara vietato.

## Verifiche

I test del proprietario coprono 27 scenari: errori delle query, limiti,
allocazione fallita, indirizzi anomali, alias completi e parziali, buffer
insufficiente, descrittori errati e copertura incompleta. Controllano
anche azzeramento limitato, sentinelle adiacenti, chiave zero, metadati
invalidi e rilascio ritentabile senza doppio FreePages.

La [preparazione del passaggio](../handoff/firmware/final-map.md) ricontrolla
anche tutte le altre risorse sulla stessa fotografia.

[UEFI: AllocatePages e tipi di memoria](https://uefi.org/specs/UEFI/2.10/07_Services_Boot_Services.html#efi-boot-services-allocatepages).
