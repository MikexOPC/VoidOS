# Loader UEFI x86-64

Carica il kernel ELF dall'ESP, valida i segmenti e prepara il contratto
di avvio, la mappa di memoria e la transizione a paging e descrittori
del kernel. Il suo punto di ingresso UEFI consegna al kernel il
contratto che il kernel stesso verifica.

**Immagine ordinaria.** Il loader compilato con `-BootKernel` avvia
`kernel.elf` dopo ExitBootServices. Il percorso di avvio QEMU/OVMF
usa il loader compilato separatamente e l'ELF del profilo Meson
`ordinary`. [Build e avvio](../../../../README.md).

**Profili separati.** I probe del loader verificano casi di errore e
handoff isolati. Le relative prove non equivalgono a un boot del
kernel ordinario. [Validazione](../../../../docs/validation/consolidamento.md#boot-memoria-e-dispositivi).

**Limiti.** La lettura dell'ESP avviene nei servizi UEFI prima della
transizione. Le pagine del loader restano riservate nel kernel.
Avvio su PC fisico non verificato per l'immagine descritta.
