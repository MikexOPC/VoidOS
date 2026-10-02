# Preparazione dei segmenti ELF

## Passo attuale

`elf/load.c` prepara ogni PT_LOAD validato in pagine ottenute con
AllocatePages(AllocateAnyPages, EfiLoaderData). Copia i byte del file
all'offset della pagina richiesto dall'indirizzo virtuale ELF e azzera
tutte le pagine, inclusa la BSS e il padding. Non esegue il contenuto.
Nel percorso UEFI le pagine sono conservate dopo il rilascio del buffer
file e durante il controllo della mappa. Vengono liberate prima del
ritorno al firmware, tramite il [proprietario del kernel](../handoff/kernel-lifetime.md).
La fixture corrente ha due segmenti: occupano due pagine da 4096 byte,
oltre ai 512 byte del buffer file e ai costi interni del firmware.

## Indirizzi e limiti

L'indirizzo fisico assegnato dal firmware puo essere diverso da quello
virtuale richiesto dal kernel. Il modulo conserva entrambi, senza creare
ancora tabelle di pagina. Le flag ELF sono metadati: non sono permessi
MMU applicati. Le pagine sono dati del loader, non codice da chiamare.
La scrittura tramite indirizzo fisico si basa sull'identity mapping
previsto da UEFI x64 durante i Boot Services.

Il modulo rivalida il buffer immutabile prima delle allocazioni.
Il limite di 128 MiB comprende offset e arrotondamento alle pagine,
non soltanto la somma delle dimensioni dei segmenti. Il buffer file,
fino a 64 MiB, resta vivo durante la copia: il picco teorico puo quindi
raggiungere 192 MiB piu overhead. Il firmware puo rifiutare un'allocazione
anche sotto questi limiti. Non viene promesso che ogni ELF ammesso
possa essere caricato nella VM da 128 MiB.

## Proprieta e rilascio

Il chiamante deve inizializzare ELF_STAGED_IMAGE con Count e
AllocatedBytes a zero. Un'immagine ancora proprietaria di pagine viene
rifiutata senza cancellarne i riferimenti. File e descrittore non devono
sovrapporsi alle allocazioni o essere modificati durante la preparazione.
Il loader controlla, prima dell'adozione e della scrittura, che il nuovo
intervallo non si sovrapponga a segmenti gia posseduti, buffer file,
descrittore di proprieta o piano locale. Confronta distanze fra indirizzi,
senza somme che possano traboccare. Un risultato ambiguo segnala
EFI_DEVICE_ERROR e non diventa un secondo proprietario: il cleanup
libera solo le allocazioni precedenti effettivamente acquisite.
Resta necessario un firmware conforme per disponibilita e accessibilita
delle pagine; questi controlli non rendono affidabile un firmware arbitrario.

Un errore di allocazione provoca il tentativo di rilascio di tutte le
pagine gia acquisite. Gli output di AllocatePages fallito sono ignorati.
Il primo errore prevale; il rilascio continua per gli altri segmenti.
elf_release_segments conserva i riferimenti delle pagine che FreePages
non riesce a liberare e permette di ritentare senza doppio rilascio.
L'integrazione corrente fa un ulteriore tentativo durante la pulizia.
Se anche questo fallisce, segnala FAIL e ritorna al firmware: non puo
garantire il recupero delle pagine rifiutate dal firmware.

## Verifica e passi successivi

`test/elf/load_test.c` controlla i byte copiati, BSS, padding, sentinelle
oltre le pagine allocate, segmenti su piu pagine, indirizzi e flag,
limite arrotondato, rifiuto di immagini gia proprietarie, allocazione
parziale fallita e rilascio fallito con successivo tentativo riuscito.
Quattro casi aggiuntivi simulano indirizzo duplicato, sovrapposizione
parziale allineata o disallineata e alias del buffer file. Verificano
che il primo segmento e il file non siano sovrascritti e che un rilascio
fallito conservi un solo proprietario per il nuovo tentativo.
I test filesystem verificano anche il percorso integrato. I dieci
scenari QEMU includono il messaggio di copia e rilascio nel caso valido.

Per ripetere:

```powershell
./arch/x86/boot/uefi/scripts/build.ps1
./arch/x86/boot/uefi/test/firmware_test.ps1
```

Il primo [contratto dei segmenti](../handoff/segments.md) viene costruito
prima del rilascio. Contratto preparatorio e tabelle inattive sono ora
integrati; restano la transizione, la mappa persistente per il kernel e ExitBootServices.
Una [fotografia temporanea della mappa UEFI](../memory/uefi-map.md)
viene verificata mentre i segmenti sono ancora allocati.
Le fixture sintetiche restano nei test; la build usa ora anche un
[kernel C diagnostico](../handoff/kernel-probe.md) compilato.

## Riferimenti

- [UEFI x64 e identity mapping](https://uefi.org/specs/UEFI/2.10/02_Overview.html#x64-platforms)
- [UEFI: AllocatePages e FreePages](https://uefi.org/specs/UEFI/2.10/07_Services_Boot_Services.html)
