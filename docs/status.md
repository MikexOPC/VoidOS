# Stato di VoidOS — 26 novembre 2026

## Immagine di produzione

`ordinary`, compilata con Meson e Ninja, avvia il loader UEFI, il kernel,
init e la shell. Le verifiche attuali sono su QEMU/OVMF q35 TCG.

- BSP e AP hanno dispatcher permanenti, IRQ comuni, preemption e supporto
  **x2APIC** (abilitazione automatica via `CPUID.01H:ECX[21]` e `IA32_APIC_BASE`
  bit 10 `EXTD` su BSP e AP con accessi MSR `0x800..0x83F`, ICR 64-bit `0x830`,
  EOI `0x80B` e fallback automatico su xAPIC MMIO quando non disponibile). La
  sessione PS/2 possiede e rilascia task, stack, IRQ e binding. Il BSP resta
  attivo dopo `exit` e senza PS/2. Task e affinità usano tabelle dinamiche, con
  capacità x86 fino a 64 task e 222 vettori sottoscrivibili per CPU.
- Processi Ring 3 isolati, canali IPC e syscall protette con stack syscall
  per processo (16 KiB, mappato solo in `KernelRoot`) e sospensione sicura
  delle syscall lunghe via timer quando `IF=1`, `lock_depth == 0` e fuori da
  copia/sezioni transazionali. Il VFS carica ELF dal disco FAT32. `proc`
  verifica IPC BSP/AP, VFS, fault isolato e cleanup. L'immagine utente arriva
  a 14 MiB (3584 pagine su 8 PT), con stack da 64 KiB e guard page.
- VirtIO-blk (MSI-X, vettore `0x44`) e AHCI (MSI, vettore `0x45`) completano
  l'I/O via interrupt con attesa `sti; hlt` (fallback `pause` se `IF=0`) e
  cleanup pulito allo shutdown. FAT legge directory e LFN; scrittura e unlink
  operano sui nomi 8.3 nella root del mount.
- Controller USB **xHCI 1.2** (PCI `0C:03:30`, MSI-X/MSI vettore `0x46`,
  fallback silenzioso `KERNEL_XHCI_ABSENT` se assente) con enumerazione e
  **hotplug runtime** sul root hub (`Disable Slot` + riutilizzo pagine DMA),
  **Hub USB 2.0 esterni** (`Class 0x09`, `Route String` + `TT`),
  **HID Boot** (tastiera USB in shell testuale e GUI, mouse USB in GUI) e
  **USB Mass Storage** (Bulk-Only Transport + SCSI `READ(10)`/`WRITE(10)`/`READ CAPACITY(10)`,
  block device con cache a 64 settori con **unmount/remount VFS live** su `/usb`).
- NX, WP, SMEP, SMAP, UMIP, guard page, canary, entropia hardware e CSPRNG
  hanno verifiche nell'immagine ordinaria. KPTI esclude heap kernel, vista
  ricorsiva, backbuffer GUI e slot device dalla vista utente.
- Seriale COM1 con FIFO 16550A da 16 byte e spinlock atomico SMP (bypassato
  in panic); `KERNEL_PREEMPT_DEFERRED_LINE = 0` nei boot misurati.
- La shell offre `help`, `print`, `status`, `bridge status`, `proc`, `fl`,
  `rp`, `write`, `rm`, `sync`, `ap`, `gui`, `exit`.
- Il desktop (`gui`) avvia il compositor Slint e il Terminale come processo
  separato: aggiornamento incrementale a celle del Terminale, buffer persistente
  nel compositor, merge rettangoli basato sul costo, mapping kernel RO
  persistente del backbuffer senza `invlpg` né buffer intermedio (`IF=1`),
  attese su eventi (`SYS_GUI_WAIT` / `ipc_wait_readable`), drenaggio equo
  round-robin, coalescing mouse con resync su overflow e RTC letto una volta
  sola all'init. Il cursore serializza ripristino e composizione dell'area
  18×18 con il present; la copia dei rettangoli conserva `IF=1`.

## Lavoro corrente

**Milestone 7 — USB xHCI + HID + Mass Storage + Hub — fatta (su QEMU TCG).**
Implementato driver xHCI 1.2 (BIOS handoff, `HCRST`, DCBAA/Scratchpad, Command/Event/Transfer Ring, enumerazione e hotplug runtime sul root hub con `Disable Slot` e riutilizzo pagine DMA senza leak, Hub USB 2.0 esterni `Class 0x09` con `Evaluate Context`/`Route String`/`TT`, Control Transfer, Interrupt IN e Bulk IN/OUT) con backend x86 PCI ECAM/DMA/MSI-X (`0x46`), binding IRQ dedicato su evento `3`, tastiera USB in shell e GUI, mouse USB in GUI, e USB Mass Storage (BOT + SCSI) con `kernel_vfs_unmount` e hotplug VFS live su `/usb`. Corretto anche il controllo idle HPET/LAPIC (`x86_lapic_idle_except`) per consentire i vettori MSI/MSI-X `0x44..0x4F` pendenti in IRR all'uscita da `hlt`.
**Milestone 7 — x2APIC — fatta (su QEMU TCG e WHPX).** Abilitato x2APIC su BSP
e tutti gli AP con fallback automatico su xAPIC; verificati timer LAPIC, IPI
INIT/SIPI, IPI di servizio/TLB shootdown, console wake AP1→BSP, NMI panic SMP e
multi-app GUI.
**Desktop, Rendering 2D e GPU Interface (`SYS_GPU_INFO`) — fatta (su QEMU TCG).**
Introdotta syscall `SYS_GPU_INFO` (`29`, ABI 64 byte `X86_GPU_INFO`: backend `VirtIO-GPU 2D` `0x1AF4:0x1050` o `UEFI GOP SW`, capabilities 2D, contatore `present_calls`) e primitive di blitting 2D SSE2 in `libui` (`blit_span_opaque`, `Canvas::blit_rect`, `Canvas::blit_alpha_rect`). Esteso il compositor Slint con Window Manager interattivo (drag finestre via titlebar in-place, z-order reale su focus, minimize/restore dal dock). Aggiunto il File Manager Ring 3 reale (`/bin/files.elf`, `kind == 2` nel dock: lettura `/` e `/usb` via `SYS_LIST_DIR`, navigazione sottodirectory e preview file) e visualizzazione del backend GPU e contatore present in System Monitor (`/bin/sysmon.elf`, `kind == 1`). Verificati in parallelo `ui`, `terminal`, `sysmon` e `files` su ogni boot ordinario e rilevamento `KERNEL_VGPU_PCI_PRESENT` + `KERNEL_VGPU_MAPPING_LIVE` su `-device virtio-vga`.
Restano: prova interattiva WHPX/SDL e hardware fisico (xHCI USB, x2APIC).
Il cancello "base solida" (10/10 boot + soak da 30+30 minuti senza errori né
perdite di memoria) è **chiuso** su QEMU TCG. L'ingresso interattivo è `gui`.

## Limiti noti

- Hardware fisico e rete non sono ancora stati provati o implementati.
- USB xHCI supporta un livello di hub USB 2.0 esterni (niente hub a cascata
  multi-livello né hotplug dinamico sulle porte downstream di un hub) e un
  dispositivo USB Mass Storage alla volta (settori 512 byte, LUN 0).
- Lo scheduler usa ancora un ticket lock condiviso (rank 900). La contesa
  misurata su 4 CPU è 0,45% in boot ordinario e 0,63% (`172.650 / 27.271.834`)
  su 60 minuti di soak (30 min attivi + 30 min idle, picco 25.856 poll); la
  migrazione a runqueue per-CPU resta lavoro futuro.
- Le syscall transazionali su lease fisiche (`SYS_SPAWN`, `SYS_DISPLAY_ACQUIRE`,
  `SYS_SHM_*`, `SYS_PROCESS_STOP`) disabilitano la sospensione syscall per
  mantenere coerente il budget fisico.
- GUI e creazione dei suoi processi operano sul BSP; input PS/2 e USB HID,
  rendering software (heap compositor Slint adattivo fino a 16 MiB, SHM fino a
  32 MiB). Gli AP mantengono i percorsi IPC supportati.
- L'uptime parte dalla prima richiesta display del kernel e accumula i wrap
  ACPI PM. Ogni turno utente ha un budget massimo compatibile con mezzo
  periodo del contatore PM a 24 bit.
- Heap kernel 32 MiB, 1024 lease fisiche e 2048 extent. Il primo init resta
  incorporato; i programmi successivi vengono caricati dal VFS.
- La vista utente conserva le mapping supervisor minime per le transizioni.
  Le mitigazioni speculative dipendono dalle capability esposte dalla CPU.
- Dubbio accettato il 15 novembre 2026: in 1 sessione su 21 con tre QEMU
  paralleli un AP non ha confermato lo shootdown TLB entro 1 s (`K4TLBACK`).
  Causa ignota; nove ripetizioni diagnostiche non lo hanno riprodotto.

## Prove e prossimo passo

Verifiche correnti sull'immagine di produzione (x2APIC + xHCI HID/MSC/Hub + GUI/GPU 2D):
33/33 test host (`meson test`, ASan inclusi `libvoid_rust`, `libui_rust` SSE2,
`xhci_host` e `vfs_fat_host`), 10/10 boot `--ordinary-shell --smp 4`
(`qemu-runner-18ebb6015c9a398c-3fc0`, con self-test parallelo di `ui`, `terminal`,
`sysmon` e `files`), 1/1 boot multi-app `--ordinary-gui 1 --smp 4`
(`qemu-runner-18ebb5dffc52c04c-8828`), 1/1 boot live `-device virtio-vga`
(`KERNEL_VGPU_PCI_PRESENT` + `KERNEL_VGPU_MAPPING_LIVE` + `sysmon` + `terminal`),
prove live QEMU `usb-storage`, hotplug VFS `/usb` e `-device usb-hub`,
3/3 boot fallback xAPIC (`-cpu max,-x2apic`), 3/3 boot WHPX headless in x2APIC,
e 1/1 soak continuo `--ordinary-soak 30 --smp 4`
(`qemu-runner-18eba603e3ccafc4-2fc8`, 30+30 min, 248 round, 0 overruns, 0 perdite).
Validazioni in [gui.md](validation/gui.md),
[consolidamento.md](validation/consolidamento.md),
[processi.md](validation/processi.md), [storage.md](validation/storage.md),
[exec.md](validation/exec.md) e [sicurezza.md](validation/sicurezza.md).
Prossimo passo: accelerazione GPU 2D/3D (VirtIO-GPU command submission / Mesa), rete o PC fisico.
