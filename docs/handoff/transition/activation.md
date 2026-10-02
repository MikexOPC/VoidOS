# Primo ingresso nel kernel dopo ExitBootServices

Il percorso completo e disponibile con `build.ps1 -BootKernel`. Carica
kernel.elf, verifica tutte le risorse, termina i servizi UEFI, attiva il
proprio ambiente x64 e chiama l'ingresso ELF. Il kernel diagnostico restituisce
un marker e la CPU si arresta nel codice posseduto del bootloader.
La build senza opzioni conserva il percorso diagnostico con cleanup e
ritorno al firmware per le regressioni dei percorsi di errore.

```powershell
& .\arch\x86\boot\uefi\scripts\build.ps1 -BootKernel
& .\arch\x86\boot\uefi\test\kernel\firmware_test.ps1
```

Il test avvia QEMU senza finestra e verifica il risultato tramite monitor.
Non basta vedere KERNEL_HANDOFF_READY: quel messaggio precede l'uscita.
Il successo richiede stato Exited con LastStatus zero, root CR3 corretta,
NXE/WP attivi e marker restituito dall'ingresso ELF.

## Risorse raggiungibili durante la transizione

Il firmware x64 usa indirizzi in identita per le regioni della mappa.
DescriptorBase nel nuovo piano mantiene GDT/TSS/IDT e due stack ausiliari
in identita; CapsuleBase mantiene una pagina EfiLoaderCode distinta R/X.
La copertura di quest'ultima respinge attributi XP, RP, RO e runtime prima
della copia del codice, e viene verificata di nuovo sulla fotografia finale.

L'owner possiede undici pagine EfiLoaderData piu una EfiLoaderCode: 48 KiB.
Le pagine fisiche 1 e 6 dei dati sono guardie non mappate nel nuovo grafo;
rimangono possedute e protette da alias dell'allocatore delle tabelle.
Prima del cambio CR3 i permessi e le guardie dipendono dal paging firmware.
Non vengono creati alias scrivibili del codice nel nuovo grafo.

Il contesto di 56 byte risiede a offset 1024 della pagina descrittori,
separato dal blocco di 704 byte. Contiene root, ingresso, stack e indirizzi
del contratto e degli operandi. La preparazione verifica risorse, CPU e
alias con i metadati prima della scrittura. Dopo l'ultimo aggiornamento
della mappa nessuno di questi indirizzi cambia.

## Sequenza CPU e contratto

1. Il chiamante esegue preflight, registra i dati diagnostici e usa
   boot_exit_memory_handoff. Ogni tentativo aggiorna fotografia e contratto.
2. Solo Exited permette il salto; Restricted impone arresto senza console,
   rilasci o ritorno EFI. Available permette ancora cleanup normale.
3. L'adattatore serializza il codice copiato con CPUID, poi entra a offset
   640 della pagina posseduta. Da qui non vengono chiamati firmware o C.
4. CLI/CLD, verifica del profilo CR4, attivazione NXE/WP, LGDT, LTR, LIDT
   e ritorno far interno caricano CS=8. DS/ES/SS diventano 16.
5. Il codice verifica NXE/WP e pubblica EnabledFeatures=3 nel contratto,
   ancora scrivibile tramite la mappatura firmware. Non e una dichiarazione
   anticipata del successo di CR3: il campo rappresenta solo NXE e WP.
6. Disabilita PGE per eliminare le traduzioni globali precedenti, scrive
   CR3 e cambia immediatamente RSP. Le istruzioni successive e il contesto
   sono gia mappati nel nuovo grafo. La root viene riletta e confrontata.
7. Riserva 32 byte di shadow space e chiama KernelEntry con RCX al contratto
   virtuale R/NX. La CALL produce RSP modulo 16 uguale a 8 all'ingresso.

Un errore del tratto assembly arresta la CPU con EAX=0xB007FFFF. Un ritorno
dal kernel conserva RAX e si arresta a offset 827; il kernel diagnostico
restituisce 0x31454E4F444F534F. Un kernel successivo puo non ritornare.
Il precedente tratto finale a offset zero resta disponibile per il probe
isolato, ma il percorso completo usa la nuova CALL e un ritorno posseduto.

## Profilo supportato e prove

Rust no_std rifiuta root non allineate o fuori dal limite fisico e CR4 con
LA57, PCID, CET o PKS attivi. Il controllo assembly ripete il vincolo CR4
prima di modificare l'ambiente. Questo primo percorso non gestisce tali
profili; il chiamante li respinge prima di ExitBootServices.

Il test QEMU verifica quattro kernel caricati dallo stesso bootloader:

| Scenario | Risultato verificato |
| --- | --- |
| Normale | Uscita UEFI riuscita, nuova root, kernel C e marker |
| Scrittura del contratto | #PF, CR2 al contratto, error code 3: WP su pagina R/NX |
| Lettura guardia stack | #PF, CR2 alla guardia, error code 0 |
| Esecuzione dello stack | #PF, CR2 allo stack, error code 17: NX |

Gli errori arrivano all'handler copiato sullo stack IST1. I controlli
includono CS, TR, limiti GDT/IDT, IF=0, DF=0, NXE, WP e PGE disattivo.
SS vale 16 nel percorso normale; il cambio IST lo forza a zero nelle
eccezioni e conserva il precedente SS=16 nel frame, verificato dal monitor.
Lo stato ExitBootServices viene letto dal monitor in memoria fisica;
non si fanno chiamate firmware per ottenere evidenza dopo l'uscita.
Il solo probe NX contiene .pdata del compilatore: il convertitore ne
controlla la directory nella sezione R e la trasporta senza usarla per unwind.
La variante normale conserva il profilo PE ristretto originale.

La prova copre QEMU/EDK2 in TCG, non hardware fisico. IST1 condiviso e
arresto terminale non risolvono eccezioni annidate, NMI concorrenti o
interrupt esterni. Il passaggio fra GDTR e IDTR non e atomico e resta
un limite del profilo iniziale rispetto a firmware/hardware con eventi NMI.
L'ambiente SIMD/FPU e TLS non viene reinizializzato. Il kernel diagnostico
non e il futuro kernel ibrido: il prossimo lavoro puo ora iniziarne la base.

Riferimenti: [UEFI 2.11, servizi di boot](https://uefi.org/specs/UEFI/2.11/07_Services_Boot_Services.html),
[ambiente x64](https://uefi.org/specs/UEFI/2.11/02_Overview.html#x64-platforms)
e [Intel SDM 3A](https://cdrdv2-public.intel.com/825758/253668-sdm-vol-3a.pdf),
sezioni 4.10.4, 6.14, 9.1.3 e 9.3.
