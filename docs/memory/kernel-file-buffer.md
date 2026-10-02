# Proprietà e controlli del buffer del file kernel

Questo passo corregge le due criticita riprodotte nella review:
perdita del proprietario dopo FreePool fallito e uso di un output
AllocatePool invalido o sovrapposto prima dei controlli.

## Acquisizione

`storage/kernel_buffer.c` verifica intervallo e allineamento a 8 byte
prima di adottare il buffer. Protegge BOOT_KERNEL_IMAGE, report,
tabella di sistema, intera tabella dei boot services dichiarata da
HeaderSize, prefissi dei protocolli root/file, console e relativo Mode.
Gli output sovrapposti o numericamente invalidi non diventano risorse
possedute e non vengono passati a Read o FreePool. Restano anomalie di
firmware non conforme, per cui non si puo garantire recupero della memoria.
I controlli numerici non provano l'accessibilita di puntatori arbitrari.
L'intero buffer deve restare nella meta canonica bassa del profilo a
quattro livelli, fuori dalla pagina zero. E una restrizione del nostro
profilo, non una regola generale di AllocatePool.

## Durata e rilascio

Il buffer valido entra in FileBuffer/FileBufferBytes del proprietario
kernel. Una FreePool riuscita azzera entrambi; su errore restano intatti.
`boot_release_kernel` tenta anche il rilascio delle pagine, conserva il
primo errore e mantiene solo le risorse ancora vive per il prossimo tentativo.
Una chiamata ulteriore dopo successo non richiama i servizi firmware.

`boot_validate_kernel` riceve ora un proprietario esplicito. Non crea un
owner locale che scompare su errore. Il chiamante deve inizializzarlo a
zero, non copiarlo e mantenerlo vivo fino al cleanup.
Un proprietario gia occupato viene rifiutato senza liberarne le risorse.
Se il firmware rifiuta continuamente il cleanup, il recupero resta
impossibile da garantire; l'ingresso UEFI ha tentativi limitati.

Le fasi successive rifiutano un kernel con il buffer temporaneo ancora
presente. Il contratto del kernel resta alla versione 2: i nuovi campi
sono metadati interni del loader, non parti del contratto pubblico.
Non vengono aggiunte allocazioni. Il proprietario cresce di due campi
x64, complessivamente 16 byte; il budget del file resta 64 MiB.

## Test

`test/storage/kernel_buffer_test.c` esegue il loader reale contro un
modello di firmware. Copre NULL, overflow, disallineamento, alias completi
e parziali con sedici output, inclusi indirizzi fuori profilo;
verifica assenza di Read e FreePool sui
risultati rifiutati. Prova errori ripetuti di pool e pagine nello stesso
cleanup, nuovo caricamento su owner occupato, rilascio successivo e
assenza di doppi rilasci. Verifica anche la conservazione del buffer
nell'API di sola validazione e il blocco delle fasi successive.

Riferimento: [UEFI 2.11, AllocatePool e FreePool](https://uefi.org/specs/UEFI/2.11/07_Services_Boot_Services.html).
