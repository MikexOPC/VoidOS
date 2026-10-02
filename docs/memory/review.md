# Revisione della gestione di memoria del boot

Questa revisione riguarda mappa UEFI, decodifica delle regioni e buffer
temporaneo e persistente. Non introduce un allocatore del kernel e non
cambia CR3 o la gestione del firmware dopo ExitBootServices.

## Controlli corretti

La validazione della mappa e la lettura delle regioni verificavano size,
capacity e stride, ma non controllavano l'overflow dell'indirizzo finale
del buffer. Ora lo verificano prima di costruire puntatori ai descrittori.
Anche un buffer AllocatePool numericamente invalido viene rifiutato
prima di passarlo a GetMemoryMap; il proprietario rimane disponibile
per il cleanup.

Il buffer non deve sovrapporsi ai propri metadati o alla tabella dei
servizi. Il refresh rifiuta questi alias prima della chiamata firmware.
Un risultato AllocatePool ambiguo viene rifiutato prima di adottarlo
o scriverlo, evitando un secondo proprietario di memoria gia viva.
Il controllo della tabella usa HeaderSize, proteggendo anche la parte
successiva al prefisso di servizi che il loader dichiara in C.

## Dimensionamento condiviso

Pool e pagine persistenti usano la stessa
[libreria Rust numerica](../libraries/buffer-capacity.md).
Restano invariati il margine di otto descrittori e il massimo di 1 MiB.
Non si aggiungono heap, proprietari impliciti o passaggi di buffer a Rust.

## Verifica delle sovrapposizioni

Quando le basi fisiche sono ordinate, basta verificare il descrittore
precedente: ogni intervallo precedente e gia stato verificato.
Questo percorso e lineare e non modifica i descrittori.

Alla prima inversione dell'ordine si passa al confronto con tutti i
descrittori precedenti, mantenendolo per il resto della fotografia.
Le mappe disordinate restano supportate e il costo massimo e quadratico.
Non si assume che il firmware fornisca un ordine specifico.

Per 4096 descrittori ordinati i confronti di sovrapposizione passano da
8386560 a 4095. E una proprieta dell'algoritmo; non e una misura della
durata totale del boot o delle prestazioni del sistema operativo.
Non vengono allocate tabelle temporanee per ordinare o memorizzare copie.

## Test e limiti

Le nuove regressioni C coprono buffer con estremo non rappresentabile,
alias con metadati e tabella dei servizi, 4096 descrittori ordinati,
sovrapposizioni non adiacenti dopo un'inversione e 512 mappe generate.
Le mappe generate comprendono casi validi ordinati, permutazioni valide
e casi potenzialmente sovrapposti, confrontati con un oracolo distinto.

Le regressioni preesistenti continuano a verificare crescita, ownership,
rilasci falliti e invarianti di kernel, stack, contratto e tabelle.
I controlli numerici non certificano un puntatore arbitrario: i buffer
devono essere accessibili per tutta la capacita dichiarata, vivi e
distinti dagli output durante la chiamata. Il firmware resta tenuto a
rispettare la capacita passata a GetMemoryMap.
