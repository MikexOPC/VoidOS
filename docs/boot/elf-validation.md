# Validazione ELF64 del kernel

## Stato del passo

Il modulo `arch/x86/boot/uefi/elf/` valida un buffer immutabile e produce
un piano di caricamento. Il modulo storage legge ora `kernel.elf` dal firmware
e gli passa il buffer completo, come descritto in [lettura kernel](kernel-file.md).
Il parser non alloca, copia segmenti, cambia paginazione o esegue il kernel.

## Interfaccia

`elf_validate(file, size, plan)` restituisce `ELF_VALID` oppure un errore
distinto per argomenti, header, profilo, tabella, segmenti, sovrapposizioni,
punto di ingresso o limiti. Il chiamante deve fornire un buffer realmente
leggibile di size byte, distinto dal piano, e mantenerlo invariato durante
la validazione. Il parser non interpreta mai i byte come puntatori.

Un piano valido contiene entry point, segmenti e somma dei loro MemoryBytes.
In caso di errore Entry, SegmentCount e TotalMemoryBytes sono azzerati;
il contenuto restante del piano non e valido e non va utilizzato.
SegmentCount delimita sempre le sole voci utilizzabili.
Il piano occupa 664 byte su questo ABI x64, forniti dal chiamante.
Non ci sono allocazioni dinamiche, ricorsione o buffer grandi sullo stack.

## Regole ELF controllate

- Magic ELF, classe 64 bit, little endian, versione 1 e architettura x86-64.
- Header di 64 byte e program header di 56 byte.
- Tabella programmi e dati dei segmenti dentro il buffer, senza overflow.
- FileBytes non superiore a MemoryBytes.
- Allineamento 0, 1 o potenza di due; congruenza offset/indirizzo.
- Congruenza rispetto alla pagina da 4 KiB scelta per il profilo x86.
- Segmenti PT_LOAD ordinati per indirizzo virtuale.

I campi sono decodificati byte per byte, anche da buffer non allineati.
PT_NULL viene ignorato, come richiesto dal formato. PT_NOTE puo essere
presente: se ne controlla l'intervallo nel file, senza interpretarne i dati.
La tabella delle sezioni non viene utilizzata o validata. Questo e un
validatore del profilo di caricamento, non un validatore ELF universale.

## Scelte iniziali del progetto

- Solo ET_EXEC statico, OSABI System V 0, ABI version 0 ed e_flags 0.
- Massimo 64 MiB di file, 32 program header e 16 segmenti caricabili.
- Massimo 128 MiB per la somma delle dimensioni dei segmenti in memoria.
- Indirizzi virtuali da 4096 fino al limite esclusivo 2^47; anche la fine
  di ciascun segmento deve rimanere entro quel limite.
- Segmenti non vuoti, leggibili e senza bit di permesso sconosciuti.
- Nessun segmento contemporaneamente scrivibile ed eseguibile.
- Nessuna sovrapposizione di pagine da 4 KiB tra segmenti distinti.
- Entry point dentro i byte presenti nel file di un segmento eseguibile,
  non nella sua estensione da azzerare.
- PT_GNU_STACK ammesso solo con permessi lettura/scrittura, senza esecuzione.
- PT_DYNAMIC, PT_INTERP, PT_TLS, PT_PHDR e altri tipi non supportati
  vengono rifiutati, cosi come la numerazione estesa dei program header.

Questi limiti sono modificabili in seguito: non descrivono tutti gli ELF
validi. Il campo p_paddr non guida il piano. Indirizzi fisici, pagine libere,
collisioni con loader/firmware, tabelle di pagina e permessi reali saranno
responsabilita della futura fase di allocazione e caricamento.
La somma MemoryBytes non include arrotondamenti a pagina o altre strutture.
Superare questi controlli non autentica il kernel e non rende sicuro il
suo codice: firme e provenienza richiedono verifiche diverse.

## Test automatici

```powershell
./arch/x86/boot/uefi/scripts/build.ps1
```

`test/elf_test.c` usa un'immagine sintetica con testo e dati separati.
Verifica header e segmenti malformati, overflow, limiti, entry point,
sovrapposizioni di pagina, segmenti solo BSS e buffer non allineati.
Controlla il confine tra 16 segmenti ammessi e 17 rifiutati.
Prova tutti i 392 troncamenti prima della fine dei dati necessari e
5.000 mutazioni deterministiche, verificando gli invarianti dei risultati.
Le mutazioni non costituiscono una prova formale o fuzzing esaustivo.
Il modulo viene compilato e testato nella DLL host ed e collegato al
percorso di ingresso dell'immagine EFI, verificato anche in QEMU.

## Fonti e passo successivo

- [System V ELF: header](https://gabi.xinuos.com/elf/02-eheader.html)
- [System V ELF: caricamento e segmenti](https://gabi.xinuos.com/elf/07-pheader.html)
- [ABI x86-64](https://refspecs.linuxfoundation.org/elf/x86_64-SysV-psABI.pdf)

La lettura e validazione dell'ELF completo sono implementate. Il modulo di preparazione e documentato in [segment-loading.md](segment-loading.md).


