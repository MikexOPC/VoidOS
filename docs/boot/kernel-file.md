# Lettura e validazione del file kernel

## Funzione attuale

Il loader apre `EFI/BOOT/kernel.elf` sul volume da cui e stato avviato,
legge il file completo e applica il profilo ELF64 x86-64 documentato
in [validazione ELF](elf-validation.md).
Prima di stampare OK tenta il rilascio del buffer e chiude file e volume.
Un errore di rilascio rende fallita l'operazione e invalida il piano.
Conserva i segmenti durante la prova della mappa, non cambia le tabelle di pagina e non
chiama ExitBootServices. Ritorna al firmware al termine della verifica.

## File di prova

Lo script build genera il probe C in `build/kernel_probe.elf` e lo copia
come `kernel.elf` nel volume build/esp. Il probe e caricabile ma non viene
ancora eseguito dal boot. La generazione lo sostituisce a ogni build:
per ora il volume e dedicato alle prove. La fixture e definita in
`test/fixtures/elf_fixture.ps1`; i test C usano la propria fixture condivisa.

## Dimensione e memoria

`GetInfo` con EFI_FILE_INFO restituisce la dimensione prima dell'allocazione.
Il loader usa un buffer locale di 512 byte per i metadati, sufficiente per
il nome fisso kernel.elf. Metadati troppo grandi, incompleti o privi di
terminatore UTF-16, directory e file fuori dall'intervallo 64 byte–64 MiB
vengono rifiutati prima di allocare il contenuto.
Non si alloca una seconda copia per i metadati.

Il contenuto usa una sola AllocatePool di tipo EfiLoaderData, pari alla
dimensione dichiarata. Ogni richiesta Read e limitata a 64 KiB.
Le letture parziali avanzano il buffer; una fine anticipata e un errore.
Dopo i byte dichiarati una lettura di un byte deve restituire zero:
un file cresciuto viene rifiutato. La verifica non garantisce una fotografia
atomica di un file che un altro soggetto modifica contemporaneamente.

Per la fixture il buffer contenuto e di 512 byte. Nel caso limite puo
raggiungere 64 MiB, oltre al costo interno dell'allocatore UEFI.
Il report e il piano sono locali. Le pagine dei segmenti vengono allocate,
copiate, azzerate e rilasciate come descritto in [preparazione segmenti](segment-loading.md).
Il file viene aperto in sola lettura.

## Risorse ed errori

`boot_open_volume` centralizza l'accesso al volume per entrambe le prove.
`boot_load_kernel` controlla prima la presenza di AllocatePool/FreePool,
apre il file, controlla GetInfo, legge, valida e percorre sempre la chiusura
delle risorse acquisite dopo le operazioni riuscite.
I valori di output di Open e AllocatePool falliti non vengono trattati
come risorse acquisite. Il primo errore prevale sugli errori di pulizia;
gli altri tentativi di pulizia continuano comunque.

Il loader dipende da protocolli firmware conformi: non puo chiudere un
handle che non espone Close. Se FreePool fallisce, non puo dichiarare la
memoria liberata: restituisce l'errore e conserva puntatore e dimensione
in BOOT_KERNEL_IMAGE. boot_release_kernel permette un successivo tentativo.
Prima della lettura il buffer deve avere un intervallo rappresentabile,
allineamento a 8 byte e non sovrapporsi a proprietario, report, tabelle
firmware, console o prefissi dei protocolli file. Output ambigui del
firmware vengono rifiutati senza Read, adozione o FreePool.

Il report conserva FileBytes solo dopo un controllo dimensione riuscito.
Validation ha valore significativo quando il parser viene raggiunto;
negli altri casi resta ELF_BAD_ARGUMENT. Il piano va usato soltanto con
EFI_SUCCESS: Entry, SegmentCount e TotalMemoryBytes sono azzerati su errore.
Il piano contiene solo valori numerici, senza puntatori al buffer liberato.
Le pagine appartengono al [proprietario del kernel](../handoff/kernel-lifetime.md),
che deve essere rilasciato anche quando boot_load_kernel fallisce.

## Output

```text
# ============ validazione kernel ============ #

[ INFO ] Lettura e controllo di EFI/BOOT/kernel.elf

# ============ preparazione segmenti ELF ============ #

[  OK  ] Segmenti copiati e BSS azzerata; pagine conservate.

[  OK  ] ELF64 verificato; buffer e file rilasciati.

[ WARN ] Avvio del kernel non ancora implementato.
```

Il ramo di errore stampa `[ FAIL ] validazione kernel fallita.` e
restituisce il relativo stato UEFI anche se la stampa fallisce.

## Test

```powershell
./arch/x86/boot/uefi/scripts/build.ps1
./test/arch/x86/boot/uefi/firmware_test.ps1
```

I test C coprono metadati, directory, limite dimensione prima dell'allocazione,
allocazione fallita, letture parziali, limite di 64 KiB per richiesta,
fine anticipata, byte extra, ELF malformati, errori di rilascio e chiusura,
priorita degli errori e percorso integrato dal punto di ingresso.
Controllano inoltre gli offset ABI di GetInfo e AllocatePool.

In QEMU vengono provati dieci scenari: fixture valida, probe C, file di prova
mancante/alterato, kernel mancante, troncato, oltre 64 MiB, con segmento
malformato o firma ELF errata, e CPU senza NX.
Il caso troppo grande usa un file da 64 MiB + 1 byte; i log e gli artefatti
rimangono in build/firmware-test-<id>/.

## Fonti e prossimo passo

- [UEFI: file e GetInfo](https://uefi.org/specs/UEFI/2.10/13_Protocols_Media_Access.html)
- [UEFI: AllocatePool e FreePool](https://uefi.org/specs/UEFI/2.10/07_Services_Boot_Services.html)

La preparazione dei segmenti e implementata in [segment-loading.md](segment-loading.md).
Le mappature sono costruite e verificate ma restano inattive.
Il trasferimento al kernel non e ancora implementato.



