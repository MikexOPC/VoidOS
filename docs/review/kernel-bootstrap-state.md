# Revisione della prima base kernel

Data: 2 ottobre 2026. Base Git: 3c1495220748a02f487554d0e4b457728cb1e7d3.

## Implementazione verificata

Il codice di produzione e ora distinto dal probe: ingresso e UART in
arch/x86/kernel/, inizializzazione, contratto, inventario e panic in kernel/.
Rust no_std valida intervalli e classificazione attraverso soli scalari.
Il kernel usa il loader gia disponibile con ExitBootServices e root propria,
stampa autonomamente, conserva i prestiti e si arresta senza tornare a UEFI.

L'ELF e di 16384 byte. I quattro segmenti occupano quattro pagine nel
profilo corrente, 16 KiB, esclusi stack e risorse del loader. La sezione
dati/BSS e di 160 byte prima dell'arrotondamento a pagina. Nessun heap,
allocazione fisica o reclamazione e stata introdotta.

## Prove completate

- Build C e Rust senza runtime, con warning trattati come errori.
- Test host del contratto, root e ingresso discordanti, formato invalido,
  output immutato su errore e alias respinti.
- Inventario con stride esteso, descrittori non ordinati, sovrapposizioni,
  limiti fisici, overflow, regione nulla e memoria loader/runtime riservata.
- UART assente e bloccata, timeout e reset ANSI prima del testo.
- Tre test Rust, incluse tutte le posizioni dei bit attributo e 1024
  confronti di intervalli adiacenti/sovrapposti.
- Convertitore: immagine valida e sette mutazioni respinte, inclusi import,
  codice scrivibile, limiti, ingresso, metadati e troncamento.
- Tre scenari QEMU: kernel pronto con 128 e 256 MiB, panic intenzionale
  con 128 MiB. Verificati messaggi autonomi, root, stato UEFI Exited con
  LastStatus zero, stack kernel, selettori, HLT e marker terminali distinti.

La prova QEMU finale usa -UseInstalled: i due casi normali caricano
esattamente l'ELF installato nella ESP. La variante panic viene costruita
separatamente. Con 128 MiB risultano 0x5054 pagine candidate, con 256 MiB
0xd054: l'incremento e esattamente 128 MiB. Il conteggio riservato include
MMIO e non rappresenta consumo di RAM.

La prima prova ha scoperto una classificazione troppo restrittiva dei
bit cache UEFI. La specifica li descrive come capacita combinabili;
la regola e stata corretta in Rust e coperta da una regressione per 0x0f.
Una variante panic ha anche rilevato codice irraggiungibile a compilazione:
ora i due percorsi sono separati da #else, mantenendo /WX.

## Evidenze locali

Log e immagini sono artefatti ignorati, non versionati. La directory
QEMU finale e kernel/build/firmware-cf173c3b7cb04af98269566e3a4e3c74.
Gli hash identificano queste esecuzioni; non garantiscono build riproducibili.

| Artefatto | SHA-256 |
| --- | --- |
| kernel/build/kernel.elf, uguale a quello installato | 638431FBFB4CDC55ABB024C0D5626C15E5EE4DAE0DE4922C1538B18261A6ADEB |
| kernel/build/install.log | 5C6C6F5CB46E7EC4650B59BA64192B0F1228D90AB92552BCC67FF7DB0041AF94 |
| kernel/build/host.log | D26A494C599C6476CEA9EE7A1D4056612F5F4B7FBE3E1F2BA01536E8DE9AE546 |
| kernel/build/image.log | EF14CA12161259FF99B0A632C721ABE715023BA5EEB10051ED339DFA467F5CBD |
| kernel/build/firmware.log | AF71AEB69AC424F623541C7D4685ED1649A4CE928757F24382638C0C53C2EA40 |
| Immagine EFI preesistente usata nei test | D63DB421DD1F1D86CCBA9A295456F6222C43924DACFE0028D5D738781D557D50 |

## Limiti e prossima task

Verificato soltanto QEMU/EDK2, q35, CPU max e TCG. COM1 e esplicita per
questo profilo, non rilevata su hardware generico. Le eccezioni terminali,
GDT/IDT/TSS, NXE/WP e SIMD/FPU sono ancora ereditati dal bootstrap.
Non ci sono allocator, scheduler, interrupt esterni, SMP, isolamento dei
servizi o recupero da panic. Questo checkpoint non dimostra le future
proprieta di sicurezza o prestazioni del kernel ibrido.

Il prossimo passo e un allocatore fisico conservativo, preceduto da
test di ownership e intervalli. Deve preservare tutte le allocazioni
del loader e gestire il limite fisico CPU e la politica di cache prima
di esporre pagine effettivamente utilizzabili.

[Architettura e comandi](../kernel/bootstrap.md),
[specifica UEFI memoria](https://uefi.org/specs/UEFI/2.11/07_Services_Boot_Services.html)
e [UART TI TL16C550C](https://www.ti.com/lit/ds/symlink/tl16c550c.pdf).
