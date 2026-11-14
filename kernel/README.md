# VoidOS — kernel

[main.c](main.c) riceve il contratto del loader, valida il bootstrap
e passa alle fasi di [startup](startup/). Il codice comune possiede
stati e risorse. Il [backend x86](../arch/x86/kernel/) implementa
registri, paging, interrupt e transizioni di privilegio.

- [Memoria fisica](mm/physical/documentation/k2-design.md),
  [heap](mm/heap/README.md) e [lock](sync/documentation/design.md)
  gestiscono allocazioni, ownership e contesa nel percorso ordinario.
- Lo [scheduler](scheduler/documentation/design.md) esegue task
  cooperativi sul BSP durante il boot. I profili separati verificano
  la preemption da timer anche sugli AP.
- [Processi](process/README.md) collega il boot all'ELF incorporato.
  La shell ordinaria usa il bridge residente. Il diagnostico prova IPC
  tra due processi.
- [Console video](console/video/documentation/design.md),
  [PS/2](drivers/input/ps2/documentation/design.md),
  [framebuffer](drivers/video/framebuffer/documentation/design.md)
  e [VirtIO GPU](drivers/video/virtio/resources/documentation/design.md)
  documentano le rispettive interfacce e il profilo in cui operano.

Senza PS/2, la build ordinaria raggiunge `KERNEL_IDLE` con interrupt
abilitati. Restano aperti il dispatcher permanente, la preemption su tutte le CPU
e il caricamento di programmi da file. [Stato e prove](../docs/status.md).
