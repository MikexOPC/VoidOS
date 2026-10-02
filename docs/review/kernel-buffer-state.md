# Verifica buffer kernel del 2 ottobre 2026

Corretti i due difetti riprodotti nella review: proprieta persa dopo
FreePool fallito e output AllocatePool non verificato prima della lettura.
Il proprietario kernel conserva ora puntatore e dimensione su errore.
Anche la validazione con rilascio immediato richiede un owner del chiamante.

Il loader rifiuta sedici output invalidi prima di Read e FreePool,
inclusi intervalli fuori dal profilo canonico basso e nella pagina zero.
I test eseguono il codice C reale con un modello indipendente dei pool
e delle pagine vive: coprono errori combinati, ritentativi, owner occupato
e assenza di doppio rilascio. Le fasi successive richiedono che il buffer
temporaneo sia gia stato rilasciato.

Build MSVC /W4 /WX e Rust con warnings vietati superata, inclusa tutta
la suite precedente. QEMU/EDK2 ha superato 10/10 scenari sullo stesso EFI.
L'immagine misura 57344 byte. Non ci sono allocazioni aggiuntive;
BOOT_KERNEL_IMAGE cresce di 16 byte, senza modificare il contratto v2.

| Artefatto in arch/x86/boot/uefi/build/ | SHA256 |
| --- | --- |
| esp/EFI/BOOT/BOOTX64.EFI | 63FA34841137E1F087CD64DF2E9B539567D81ADFC55108699E99E27C687B4363 |
| buffer-build.log | 682380BD9390DC39F195F0E8DA232CF079915F5C351E3124D755DA2C42CC01A9 |
| buffer-firmware.log | 198E072313F5906B2AD2B3FA1598AD08E594C6883363E7E01B8922FF8AE85FFC |

Log seriali: `firmware-test-033967ad75254844a15f97bfc060b757/<scenario>/serial.log`.
I log grezzi restano locali in build/, ignorati da Git.

Un firmware che rifiuta ogni rilascio puo comunque lasciare risorse vive;
il chiamante deve mantenere l'owner fino al cleanup. Le guardie non
certificano l'accessibilita di puntatori arbitrari. CR3 e descrittori
restano invariati nel percorso reale; il kernel non viene eseguito.

Vedere [gestione del buffer](../memory/kernel-file-buffer.md) per API,
proprieta e riferimento ufficiale UEFI.
