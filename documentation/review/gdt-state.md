# Verifica della bozza GDT del 2 ottobre 2026

La modifica introduce la libreria C dei descrittori x64, la fase di
preparazione nel boot e i test C/C++. La bozza resta sullo stack corrente
e non viene attivata. Non ci sono nuove allocazioni firmware.

Build completa con MSVC /W4 /WX e Rust con warnings vietati superata.
Passati i test precedenti e quelli nuovi: 272 mutazioni, 512 basi
riproducibili, due estremi, guardie degli output e 24 alias.
Il test dell'ingresso completo attende ora 13 sezioni, inclusa la GDT.

QEMU/EDK2: 10/10 scenari superati. Nei due casi validi lo script richiede
la fase dei descrittori dopo il preflight UEFI e prima del cleanup.
L'immagine misura 55808 byte, 1536 in piu del checkpoint precedente.

| Artefatto in arch/x86/boot/uefi/build/ | SHA256 |
| --- | --- |
| esp/EFI/BOOT/BOOTX64.EFI | 01C5C0B90B2ECBE2F0E1598478C538F18D7652F99D5490B680DD9F444C007FB2 |
| gdt-build.log | 540ACFA02F3A1143133FBF9C2773ED24B0C38CCA11DEFCCC7E6B06D912EF6B70 |
| gdt-firmware.log | 2889A77F299CE00B04CFD3B4033F981E579502C548B2D8FE17F65F7EFA3C0E16 |

Log seriali: `firmware-test-8f550990354b4349b6a4d7ff2b91c636/<scenario>/serial.log`.
I log grezzi restano artefatti locali ignorati da Git.

La revisione dei bit segue il [manuale Intel](https://cdrdv2-public.intel.com/825758/253668-sdm-vol-3a.pdf).
La [documentazione del componente](../handoff/descriptors/gdt.md) chiarisce
durata, formato, proprieta e limiti. Queste prove non eseguono LGDT,
non cambiano CS/SS o CR3 e non dimostrano il futuro ingresso nel kernel.
