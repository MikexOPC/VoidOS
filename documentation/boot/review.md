# Controllo del bootloader del 1 ottobre 2026

## Ambito e risultato

Controllati i tipi e gli offset UEFI usati, il parser ELF, l'allocazione
delle pagine, la copia, la BSS, la pulizia delle risorse e la console.
Non sono stati trovati errori nei calcoli degli intervalli o della
dimensione delle pagine nel profilo attualmente accettato.

La scelta di AllocateAnyPages ed EfiLoaderData e coerente con UEFI.
L'accesso alle pagine attraverso l'indirizzo fisico si basa sull'identity
mapping richiesto per x64 prima di ExitBootServices. Non e una promessa
valida per ogni architettura o dopo il cambio delle tabelle di pagina.
Il parser impone un profilo piu ristretto del formato ELF generale;
i segmenti rifiutati non sono necessariamente ELF invalidi.

## Correzioni

- Un fallimento dopo la validazione ELF ora produce "preparazione kernel
  fallita", evitando di attribuire un errore di allocazione al parser.
- Se il cambio di colore riesce ma il ripristino fallisce, la console
  restituisce errore prima di stampare il corpo del messaggio.
- I separatori usano dodici = per lato e il colore normale in UEFI e
  PowerShell. La dimensione dei caratteri non e stata modificata.

## Verifiche

Compilazione MSVC x64 con /W4 e /WX e controlli dell'immagine PE EFI.
Test host su ingresso, filesystem, ELF, colori e caricamento; comprendono
troncamenti e 5.000 mutazioni ELF deterministiche.
Nuovi casi verificano gli offset AllocatePages/FreePages (40/48),
indirizzi non allineati senza scritture, rilascio parziale con tentativo
successivo senza doppio FreePages, callback mancante, errore di ripristino
colore e diagnostica dell'allocazione fallita nel percorso di ingresso.

La prova firmware usa QEMU 11.1.0 con EDK2, dieci volumi separati e ritorno
al firmware verificato. Il test della copia controlla i byte in memoria
tramite allocazioni simulate; QEMU verifica l'esecuzione integrata e i log.

## Limiti effettivi

E una prova di preparazione e rilascio, non un kernel avviato. Non ci sono
ancora attivazione delle mappature e dei permessi MMU, ExitBootServices, autenticazione
del kernel o driver. Un firmware che rifiuta ripetutamente FreePages puo
lasciare pagine allocate: il loader segnala errore ma non puo garantire
il recupero. I test non costituiscono una prova formale o una verifica
su PC fisico. Prima del salto al kernel serviranno ulteriori controlli.

## Fonti ufficiali

- [UEFI: allocazione e rilascio](https://uefi.org/specs/UEFI/2.10/07_Services_Boot_Services.html)
- [UEFI: ambiente x64](https://uefi.org/specs/UEFI/2.10/02_Overview.html#x64-platforms)
- [UEFI: console testuale](https://uefi.org/specs/UEFI/2.10/12_Protocols_Console_Support.html)
- [System V ELF: segmenti e BSS](https://gabi.xinuos.com/elf/07-pheader.html)
