# Uscita dai Boot Services: modulo preparatorio

`handoff/firmware/exit_services.c` implementa la sequenza di uscita con
un buffer memoria gia allocato e tentativi limitati. Non e ancora chiamato
dall'ingresso UEFI reale: senza transizione pronta, il loader non potrebbe
ritornare al firmware o continuare con i suoi servizi dopo il successo.

## Sequenza e stati

Il chiamante fornisce ImageHandle, prefisso dei servizi esteso fino a
ExitBootServices, mappa con buffer posseduto e stato inizialmente azzerato.
I servizi devono essere ancora disponibili; gli oggetti sono distinti,
vivi e non modificati esternamente durante la chiamata.

Ogni tentativo esegue GetMemoryMap, valida la fotografia in C e chiama
ExitBootServices con la chiave appena ottenuta. Fra le due chiamate non
esegue altri servizi. Solo EFI_INVALID_PARAMETER permette un nuovo
tentativo, con una nuova fotografia nello stesso buffer. Il limite del
progetto e tre tentativi, non una garanzia di successo del firmware.

- Available: nessun tentativo di uscita; un errore iniziale consente ancora
  al chiamante il normale percorso di cleanup e diagnostica.
- Restricted: e stato tentato ExitBootServices. Anche su errore il firmware
  potrebbe aver disattivato parzialmente i servizi. Il chiamante non deve
  usare il normale percorso di console, filesystem o ritorno al firmware.
- Exited: uscita riuscita; nessun Boot Service o protocollo firmware puo
  essere chiamato. Buffer e risorse restano vivi per il kernel.

Il modulo non usa console, callback di diagnostica, allocazioni o rilasci.
Se la mappa cresce oltre il buffer dopo il primo tentativo, termina in
Restricted con EFI_BUFFER_TOO_SMALL. In questo profilo il futuro chiamante
dovra arrestarsi autonomamente; non si tenta un recupero generale.
La specifica permette alcuni servizi memoria dopo un fallimento parziale,
ma il nostro modulo usa soltanto GetMemoryMap nei tentativi successivi.

Dopo ogni uscita fallita invalida la fotografia. Dopo il successo mantiene
dimensione, stride, versione e chiave del buffer utilizzato. Il proprietario
del buffer non deve chiamare FreePool o FreePages in Exited. Il cambio CR3 non viene
effettuato da questo modulo. Le altre risorse devono essere gia pronte:
non e consentito attivare questo modulo nel percorso UEFI attuale.

Il percorso attuale prepara una [mappa su pagine dedicate](final-map.md)
e controlla la copertura di tutte le risorse, ma non la mappa ancora nella
root futura e non consegna i descrittori nel contratto del kernel.

## Verifiche e ABI

L'interfaccia x64 usa un prefisso separato di 240 byte; ExitBootServices
si trova all'offset 232. Il prefisso originale di 160 byte resta invariato.
I test verificano offset e dimensioni rispetto all'ordine dei servizi UEFI.

Dodici scenari host simulano successo immediato, chiave zero opaca, uno o due cambi di chiave,
chiave sempre scaduta, errori di uscita, crescita e corruzione della mappa
dopo un tentativo, ed errori prima di ogni uscita. Una traccia deve contenere
solo coppie GetMemoryMap/ExitBootServices, con al massimo una lettura fallita
finale. Allocatori e FreePool presenti nel mock segnalano ogni chiamata
vietata; dopo il successo il mock rifiuta ogni servizio. Un nuovo ingresso
nel modulo con stato Restricted o Exited viene rifiutato senza chiamate.

Questi test non dimostrano ancora ExitBootServices su QEMU o PC fisico.
QEMU continua a verificare preparazione e ritorno al firmware.

## Riferimenti

- [UEFI: ExitBootServices](https://uefi.org/specs/UEFI/2.10/07_Services_Boot_Services.html#efi-boot-services-exitbootservices)
- [EDK2: ordine e tipi dei Boot Services](https://github.com/tianocore/edk2/blob/master/MdePkg/Include/Uefi/UefiSpec.h)
