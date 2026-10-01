# Verifica del contenuto delle tabelle inattive

`paging/verification/contents.c` verifica le tabelle dopo il controllo
della copertura nella mappa UEFI e prima di pubblicare il contratto.
Non alloca memoria, non modifica le tabelle e non scrive CR3.

Prima degli accessi verifica piano, larghezza fisica e metadati del
proprietario: indirizzi allineati, distinti, nei limiti e senza alias
di kernel, stack o contratto. Il chiamante deve fornire pagine possedute,
leggibili attraverso il mapping UEFI e non modificate durante la verifica.
Le tabelle devono essere inattive: le voci sono controllate nel profilo
esatto appena costruito, senza bit Accessed o Dirty impostati dalla CPU.

## Controlli

La visita ha al massimo quattro livelli e 128 pagine di tabelle. Prima
di seguire un indirizzo intermedio lo cerca nell'elenco delle pagine
possedute: un indirizzo estraneo non viene dereferenziato. Una bitmap
di 128 byte impedisce cicli e riuso della stessa tabella su piu rami.

Le voci intermedie devono essere esattamente supervisor Present/RW,
con indirizzo della pagina figlia. Ogni foglia deve corrispondere al
piano per indirizzo virtuale, indirizzo fisico e tutti i bit di permesso.
Vengono rifiutati huge page, User, Global e bit aggiuntivi. Il confronto
copre anche R/NX del contratto e RW/NX dello stack.

Il numero delle foglie deve uguagliare quello delle pagine del piano;
tutte le pagine delle tabelle devono essere raggiungibili dalla root.
Questo rifiuta pagine mancanti e mappature estranee, inclusa una guardia
stack accidentalmente presente. Un piano validato non contiene alias.

## Errori e verifiche

Metadati non validi producono EFI_INVALID_PARAMETER; contenuti corrotti
producono EFI_DEVICE_ERROR. Il verificatore non cambia il proprietario.
Nel percorso boot l'errore invalida root e contratto e porta al cleanup.

I test alterano venti casi: foglie mancanti, indirizzi o permessi errati,
contratto scrivibile/eseguibile, User, Global, huge page, indirizzi esterni,
cicli, rami condivisi, guardia mappata e metadati non validi. Verificano
anche il profilo massimo di 128 MiB con 16 segmenti e il contratto.
Un caso integrato corrompe una voce durante la fotografia firmware:
il boot deve rifiutarla e tentare il rilascio di tutte le risorse.

Questa verifica controlla il risultato del costruttore; non sostituisce
la mappatura del trampoline, le eccezioni o ExitBootServices. Le tabelle
non vengono ancora attivate.

[Intel SDM, Volume 3A](https://cdrdv2-public.intel.com/825758/253668-sdm-vol-3a.pdf),
capitolo 4: paging a quattro livelli e permessi delle voci.
