# Checkpoint: pagina kernel posseduta e RW/NX

Revisione del 2 ottobre 2026. Base Git: 86cd538. Task completata:
vista ricorsiva del paging, ownership bloccata durante la mappatura,
azzeramento verificato, rimozione PTE e invalidazione prima del rilascio.
L'allocatore rimane C, con politica delle regioni firmware validata in Rust;
gli accessi x86 privilegiati rimangono in assembly.

## Revisione e correzioni

Lo slot ricorsivo inizialmente scelto, 255, coincideva con indirizzi
dei mock DLL Windows. Il test integrato dei descrittori lo ha rilevato.
Lo slot 128 riserva un intervallo distinto; tutti i test di build sono
stati ripetuti. Nessuna collisione viene accettata dal piano del loader.

Il rilascio della finestra controlla anche l'ID vivo e il pin prima di
rimuovere la PTE. Un handle alterato viene respinto senza cambiare il TLB
o liberare il frame. Voci inattese nei genitori o nella PTE falliscono
senza mutazioni; la verifica del loader ammette un solo ciclo ricorsivo
con indirizzo e permessi esatti. I bit A/D hardware sono accettati solo
dal verificatore delle tabelle gia attive del kernel.

La geometria fissa e controllata dall'header del profilo; le guardie sono
riservate dal piano e devono essere assenti. Non viene creato un alias
scrivibile di codice eseguibile. I frame candidati escludono tutte le
risorse ancora possedute dal loader tramite i tipi della mappa UEFI.

## Evidenza sullo stato finale

- Build completa boot C/C++/Rust, inclusi 24 casi di corruzione tabelle
  e rifiuto di segmenti nelle nuove finestre riservate.
- Host kernel: contratto, inventario, 1024 sequenze fisiche, mapper,
  ordine pin/PTE/TLB/rilascio, OOM, handle alterati, parent corrotti,
  UART/colori e quattro test Rust.
- Conversione ELF: immagine valida e sette mutazioni invalide.
- Boot diagnostico: 10/10 scenari QEMU sulla stessa immagine diagnostica.
- Handoff reale: 4/4 scenari QEMU, inclusi WP, guardia stack e NX.
- Kernel finale installato: 5/5 scenari QEMU, con 128/256 MiB, panic,
  NX sulla pagina nuova e lettura dopo unmap. Le ultime due prove
  controllano #PF, CR2, error code 17/0 e lo stack IST.
- `git diff --check` superato; file modificati normalizzati LF.

I log sono artefatti ignorati sotto build/. Per la verifica finale:
kernel/build/firmware-d6321b4b11f545ba9020c5d8ebcb5a45 e
arch/x86/boot/uefi/build/kernel-test-0f2ba458ee20413e9e297d2dfb84bbd4.
Il test diagnostico e in firmware-test-8eb210c016e946479dbb374c6d733f1b.
Le immagini ordinarie sono state ripristinate dopo i test diagnostici.

| Artefatto finale | SHA-256 |
| --- | --- |
| BOOTX64.EFI, 75264 byte | 306E68050D0A12636B31C15103444335E026DE8155D4E269623F64D1D2DA5D3A |
| kernel.elf, 24576 byte | F1DCE8D4FCDD8675073E5826416C36C0479AE2848A4A2E981926341321BCD127 |
| mapping-host.log | 347804EDAE3090F5FA5405F28AF306A86DA38BB5DBAB2B6DAEB3C49E43FF0627 |
| mapping-final-firmware.log | 170DDD73DC9D9FB555B1492FC3A3F25AB76E0B3970E52AC62DFDA0B397E3B102 |
| mapping-final-build.log, boot | 4502BEC543FC5BDF66C6C0616DE6DCC5BAFCE31423D1912B085AFD370FEA7115 |
| mapping-final-protection.log | 3214729C9FFBD3B2C6C6F0E6130712E3B443D8E37ECD63FF330BC6419BCA6C6A |

Gli hash identificano queste esecuzioni, non garantiscono build riproducibili.
Il kernel ha quattro segmenti ELF, 28 KiB di memoria residente arrotondata
per segmento. L'owner fisico occupa 6448 byte, compresi i 256 byte dei pin.
La finestra consuma un frame di 4 KiB solo mentre e attiva e riusa una PT
esistente. I test ripristinano interamente il budget delle pagine libere.

## Limiti e prossima task

Solo QEMU/EDK2 q35, CPU max, TCG e un processore. Nessuna prova hardware
fisica, SMP/TLB shootdown, PCID, PAT personalizzato, memoria cifrata,
heap, paging dinamico generale, reclamazione firmware o IOMMU.
GDT/IDT/TSS ed eccezioni terminali sono ancora quelli del bootstrap.
Il pin protegge il protocollo di ownership fra chiamanti fidati;
non isola codice ring 0 ostile. Il kernel ibrido resta un obiettivo
architetturale, non una proprieta gia implementata.

Il passaggio al kernel e gia funzionante. La prossima task deve estendere
in modo limitato la memoria virtuale prima di costruire un heap, mantenendo
guardie, budget, rollback e prove reali delle invalidazioni.

[API e comandi](../kernel/memory/page-window.md),
[Intel SDM Volume 3A](https://cdrdv2-public.intel.com/825758/253668-sdm-vol-3a.pdf),
sezioni 4.5 e 4.10.4.
