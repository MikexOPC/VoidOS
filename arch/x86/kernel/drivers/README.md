# Backend dei driver x86-64

Collega le interfacce dei driver comuni a firmware, MMIO, IRQ e
ownership delle risorse. I backend qui includono PCI/ECAM, input PS/2,
framebuffer GOP, VirtIO video e Intel VT-d. I driver comuni risiedono
in [kernel/drivers](../../../../kernel/drivers/).

**Immagine ordinaria.** Il boot QEMU verificato acquisisce il
framebuffer GOP e ne prepara la mappatura quando il firmware lo
fornisce. **Profili separati.** Tastiera, mouse e shell PS/2 sono
verificati nei profili interattivi. `v2c` verifica una sessione
VirtIO video con un dominio VT-d dedicato alla GPU.
[Validazione](../../../../docs/validation/consolidamento.md#boot-memoria-e-dispositivi).

**Limiti.** I driver attivi nelle sessioni provate restano sul BSP.
Le prove VT-d riguardano il dispositivo e le configurazioni QEMU
documentate. Hardware fisico e driver a blocchi restano aperti.
