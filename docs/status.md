# Stato di VoidOS — 27 novembre 2026

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
- Rete **VirtIO-net** (VirtIO 1.2 §5.1, PCI `0x1AF4:0x1041`/`0x1000`, MSI-X
  vettore `0x47`, fallback `KERNEL_VIRTIO_NET_ABSENT` se assente) dietro
  interfaccia astratta `KERNEL_NET_DEVICE`, stack di protocolli `no_std`
  panic-free in Rust (`Ethernet II`, `ARP`, `IPv4`, `ICMP`, `UDP`, `DHCPv4`,
  `DNS` RFC 1035, `TCP` client RFC 9293), syscall `30..36` (`SYS_NET_*`),
  wrapper `libvoid` e comandi reali `net`, `ping <ip|host>`, `http <host> [path]`
  nel Terminale Ring 3 (`avvia.bat` avvia QEMU con `-netdev user -device virtio-net-pci`).
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
**Desktop, VirtIO-GPU 2D reale, cursore hardware e Window Manager — fatta (su QEMU TCG).**
Attivato il driver **VirtIO-GPU 2D** nell'immagine di produzione `ordinary` (anche senza VT-d IOMMU tramite DMA diretto e senza GOP tramite mode-setting nativo, più gestione `VIRTIO_GPU_EVENT_DISPLAY` / `kernel_gui_display_rebind`) e il **cursore hardware su `cursorq`** (64×64 `B8G8R8A8_UNORM`, `X86_GPU_CAP_HW_CURSOR`, con fallback software su GOP). Nel desktop Ring 3: Terminale stile console con font 9×18 anti-alias e comandi reali (`help`, `print`, `fl`, `rp`, `write`, `rm`, `sync`, `clear`, `exit`), blur nativo del dock generato dallo sfondo con orologio italiano (CET/CEST) e punto per app ridotte, drag fluido su coordinate assolute, wallpaper cachato 1:1, massimizzazione/ripristino e resize interattivo animati (`MSG_RESIZE_WINDOW` con ridisegno nativo in `terminal`, `sysmon` e `files`, angoli inferiori arrotondati sul Default Dock), heap SHM multi-regione in `libvoid`, layout responsive per qualsiasi risoluzione e launcher desktop con `-display gtk,zoom-to-fit=on`.
**Milestone 7 — Rete (`VirtIO-net` + DHCPv4/DNS/TCP + Ring 3) — fatta (su QEMU TCG SLIRP).**
Implementato driver `VirtIO-net` (`KERNEL_NET_DEVICE`, MSI-X `0x47`), stack Rust `no_std` panic-free (Ethernet/ARP/IPv4/ICMP/UDP/DHCPv4/DNS/TCP client), orchestrazione `kernel/net/net.c` (cache ARP 64 voci, 32 connessioni TCP con RX ring buffer da 8 KiB), syscall `30..36` validate con `x86_process_copy_range`/`x86_process_copy_user`, wrapper `libvoid` e comandi `net`, `ping` e `http` nel Terminale GUI Ring 3 (verificati live con DHCP `10.0.2.15`, ping `10.0.2.2` e `http example.com` -> `HTTP/1.1 200 OK`).
Restano: prova interattiva WHPX e hardware fisico (xHCI USB, x2APIC, NIC fisica).
Il cancello "base solida" (10/10 boot + soak da 30+30 minuti senza errori né
perdite di memoria) è **chiuso** su QEMU TCG. L'ingresso interattivo è `gui`.

## Limiti noti

- Hardware fisico non è ancora stato provato (inclusi driver NIC fisici Intel/Realtek).
- Lo stack di rete supporta solo IPv4, client TCP (niente listen/accept né
  TLS/HTTPS) e un solo NIC `VirtIO-net`, verificato su QEMU SLIRP (`-netdev user`).
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

Verifiche correnti sull'immagine di produzione (x2APIC + xHCI HID/MSC/Hub + VirtIO-GPU 2D/cursorq + Window Manager animato + Rete VirtIO-net/DHCP/DNS/TCP):
35/35 test host (`meson test`, ASan inclusi `virtio_net_host`, `net_protocol_rust`, `libvoid_rust`, `libui_rust`, `gui_host`, `xhci_host` e `vfs_fat_host`), 2/2 test `qemu-runner`, 3/3 boot `--ordinary-shell --smp 4` (`qemu-runner-18ebf17051385e84-8ff0`, fallback `-net none`), 1/1 boot multi-app `--ordinary-gui 1 --smp 4` (`qemu-runner-18ebf0ba7ff77a18-20b0`), prova live `-netdev user -device virtio-net-pci` con Terminale Ring 3 (`net`, `ping 10.0.2.2`, `http example.com` -> `HTTP/1.1 200 OK`), 10/10 boot `--ordinary-shell --smp 4` (`qemu-runner-18ebed8b9fd98e70-8198`), prove live VirtIO-GPU/USB/xAPIC/WHPX e 1/1 soak continuo `--ordinary-soak 30 --smp 4` (`qemu-runner-18eba603e3ccafc4-2fc8`, 30+30 min, 248 round, 0 overruns, 0 perdite).
Validazioni in [gui.md](validation/gui.md),
[consolidamento.md](validation/consolidamento.md),
[processi.md](validation/processi.md), [storage.md](validation/storage.md),
[exec.md](validation/exec.md) e [sicurezza.md](validation/sicurezza.md).
Prossimo passo: prova su PC fisico (boot da chiavetta USB / fallback senza PS/2 / log su GOP) oppure estensioni GUI/rete (TLS, clipboard, FAT32 sottodirectory).
