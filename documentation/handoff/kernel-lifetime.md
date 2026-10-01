# Durata delle pagine del kernel

## Proprietario unico

BOOT_KERNEL_IMAGE contiene il proprietario ELF_STAGED_IMAGE e la
descrizione numerica BOOT_SEGMENTS. Il chiamante inizializza tutto a
zero e non copia questo oggetto per valore, per evitare due proprietari
delle stesse pagine. Il report KERNEL_FILE_REPORT resta separato e
contiene solo il risultato della verifica, senza possedere allocazioni.

`boot_load_kernel` legge, valida, copia e descrive i segmenti. Libera
il buffer del file e chiude file e volume, ma conserva le pagine nel
proprietario fornito dal chiamante. Rifiuta un proprietario gia occupato
senza sovrascriverne le pagine o la descrizione. Report, proprietario e
buffer devono essere distinti e non modificati durante le chiamate.

Il chiamante deve invocare `boot_release_kernel` anche quando il
caricamento fallisce: pagine acquisite possono restare possedute dopo
un errore di pulizia o di preparazione. Una descrizione con firma zero
non si puo usare per un passaggio al kernel, anche se restano pagine.
La descrizione numerica non trasferisce la proprieta e non e una prova
di mappature o permessi MMU configurati.

## Rilascio

boot_release_kernel invalida firma, entry e conteggio della descrizione
prima di chiamare FreePages. Dopo un rilascio parziale, il proprietario
conserva i riferimenti delle pagine non liberate e puo ritentare.
Le pagine gia liberate hanno Pages zero e non sono rilasciate due volte.
Un proprietario vuoto puo essere rilasciato di nuovo senza servizi UEFI.

Il primo errore dell'operazione prevale su un errore di rilascio successivo,
che viene comunque segnalato. Se il firmware continua a rifiutare
FreePages non si puo garantire il recupero: resta un errore terminale.
Anche il buffer del file dipende da un FreePool conforme; questo passo
non modifica la gestione di quel buffer temporaneo.

## Percorso attuale

L'ingresso UEFI mantiene le pagine vive durante acquisizione,
classificazione della mappa e [verifica di copertura](resources.md).
Alloca anche uno stack dedicato. Il firmware continua a gestirle come
EfiLoaderData. Poi libera mappa e pagine prima di tornare al firmware.
Tutti gli errori successivi al caricamento, inclusi quelli di stampa,
passano per la pulizia delle pagine. Le stampe di successo sono emesse
soltanto dopo il successo del relativo passo.

Il descrittore e ancora sullo stack del loader. Non viene trasferito
al kernel, non viene chiamata l'entry della fixture e non si esegue
ExitBootServices. La chiave della mappa temporanea resta inutilizzabile
dopo la pulizia. Il wrapper boot_validate_kernel conserva il percorso
di prova con rilascio immediato per le regressioni esistenti.

## Test

I test storage verificano proprieta conservata dopo caricamento,
rifiuto di un secondo caricamento sullo stesso proprietario, rilascio
parziale con successivo tentativo riuscito e assenza di doppio rilascio.
Il servizio GetMemoryMap simulato rifiuta la prova se le pagine sono
state liberate prima dell'acquisizione. Sono verificati anche errore
della mappa ed errore console dopo il caricamento, con pulizia delle pagine.

QEMU controlla il messaggio di conservazione, il controllo della mappa
e il messaggio finale di rilascio prima del ritorno al firmware.

```powershell
./arch/x86/boot/uefi/scripts/build.ps1
./arch/x86/boot/uefi/test/firmware_test.ps1
```

[UEFI: proprieta delle risorse e rilascio prima del ritorno](https://uefi.org/specs/UEFI/2.10/07_Services_Boot_Services.html)
