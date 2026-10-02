# Verifica buffer kernel del 2 ottobre 2026

Corretti i due difetti riprodotti nella review: proprieta persa dopo
FreePool fallito e output AllocatePool non verificato prima della lettura.
Il proprietario kernel conserva ora puntatore e dimensione su errore.
Anche la validazione con rilascio immediato richiede un owner del chiamante.

Il loader rifiuta tredici output invalidi prima di Read e FreePool.
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
| esp/EFI/BOOT/BOOTX64.EFI | 6EFEA65A9C372BDFF176D91347E13FE2836A11DA0C18C72DC2905331A7643272 |
| buffer-build.log | DACB7D5C94B7390CDD05F5F216B330E6D607C4265DC889C1B3A2BE16F06D8D40 |
| buffer-firmware.log | 640CEEC75EB6455B8824ED06BA0E881FED87C526E0310D48FC6F7DA1EF4A9FD9 |

Log seriali: `firmware-test-a972863855a7443a8641215e1c71b704/<scenario>/serial.log`.
I log grezzi restano locali in build/, ignorati da Git.

Un firmware che rifiuta ogni rilascio puo comunque lasciare risorse vive;
il chiamante deve mantenere l'owner fino al cleanup. Le guardie non
certificano l'accessibilita di puntatori arbitrari. CR3 e descrittori
restano invariati nel percorso reale; il kernel non viene eseguito.

Vedere [gestione del buffer](../memory/kernel-file-buffer.md) per API,
proprieta e riferimento ufficiale UEFI.
