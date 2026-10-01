# Aggiornamento della mappa prima dell'uscita firmware

`memory/final/refresh.c` aggiorna una fotografia usando il buffer gia
posseduto. E distinto dall'acquisizione iniziale, che puo allocare o
ridimensionare il buffer. Non chiama AllocatePool, FreePool o la console.

Prima della chiamata invalida chiave e metadati precedenti, mantenendo
Buffer e Capacity. GetMemoryMap riceve tutta la capacita disponibile.
Solo dopo il successo e la validazione dei descrittori viene ripubblicato
Valid. La chiave zero non viene rifiutata: la specifica la tratta come
valore opaco da restituire al firmware.

Se il buffer diventa insufficiente, restituisce EFI_BUFFER_TOO_SMALL
senza ridimensionarlo o usare la vecchia chiave. Gli altri errori lasciano
la fotografia invalidata e il buffer ancora posseduto dal chiamante.
Il buffer deve essere vivo, scrivibile, distinto dai metadati e dai
servizi. La validazione non puo verificare un puntatore arbitrario.

## Integrazione attuale

Il boot aggiorna la fotografia dopo la costruzione delle tabelle e prima
di ricontrollare risorse e contenuti. Questa fotografia e ancora temporanea:
il buffer viene rilasciato durante il percorso di prova e non consegnato
al kernel. Allocazioni, rilasci o ulteriori attivita del firmware possono
rendere vecchia la chiave; questo refresh non certifica le fotografie future.

Il [modulo di uscita](../handoff/firmware/exit-services.md) riusa invece il buffer
nei tentativi di ExitBootServices e lo conserva in caso di successo.
Il boot prepara ora anche un [buffer persistente](persistent-map.md) in
pagine dedicate e lo verifica contro tutte le risorse. Prima dell'uscita
reale servono ancora la sua mappatura, il contratto completo e un percorso
di transizione autonomo.

## Verifiche

I test host forniscono solo GetMemoryMap: gli allocatori e i rilasci sono
assenti. Controllano aggiornamento della chiave, crescita oltre capacita,
errore del firmware, stride/versione non supportati, descrittori sovrapposti
e dimensioni non valide. Ogni errore invalida i metadati, conservando
indirizzo e capacita. Il percorso QEMU usa il refresh reale.

[UEFI: GetMemoryMap](https://uefi.org/specs/UEFI/2.10/07_Services_Boot_Services.html#efi-boot-services-getmemorymap).
