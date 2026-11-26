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
  a 6 MiB, con stack da 64 KiB e guard page.
- VirtIO-blk (MSI-X, vettore `0x44`) e AHCI (MSI, vettore `0x45`) completano
  l'I/O via interrupt con attesa `sti; hlt` (fallback `pause` se `IF=0`) e
  cleanup pulito allo shutdown. FAT legge directory e LFN; scrittura e unlink
  operano sui nomi 8.3 nella root del mount.
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

**Milestone 7 — x2APIC — fatta (su QEMU TCG e WHPX).** Abilitato x2APIC su BSP
e tutti gli AP con fallback automatico su xAPIC; verificati timer LAPIC, IPI
INIT/SIPI, IPI di servizio/TLB shootdown, console wake AP1→BSP, NMI panic SMP e
multi-app GUI.
**Desktop con app reali (Terminale + System Monitor) — fatta (su QEMU TCG).**
System Monitor gira come processo Ring 3 reale dal dock (`kind == 1`), legge
metriche vere del kernel tramite `SYS_SYS_INFO` (`28`: RAM fisica, heap kernel,
CPU online via `x86_percpu_online_count()`, processi attivi, canali IPC, uptime,
preempt ticks) e gira affiancato al Terminale senza sovrapposizione. Risolti
durante la verifica multi-app: isolamento atomico di `cpu->CopyScratch` in
`copy_checked` sotto preemption syscall, aggiornamento in-place dei pixel in
Slint (`set_row_data` senza distruggere i `TouchArea` delle altre finestre) e
sincronizzazione immediata di `close-clicked`.
Restano: prova interattiva WHPX/SDL e hardware fisico (xHCI USB, x2APIC).
Il cancello "base solida" (10/10 boot + soak da 30+30 minuti senza errori né
perdite di memoria) è **chiuso** su QEMU TCG. L'ingresso interattivo è `gui`.

## Limiti noti

- Hardware fisico, USB xHCI e rete non sono ancora stati provati o
  implementati.
- Lo scheduler usa ancora un ticket lock condiviso (rank 900). La contesa
  misurata su 4 CPU è 0,45% in boot ordinario e 0,63% (`172.650 / 27.271.834`)
  su 60 minuti di soak (30 min attivi + 30 min idle, picco 25.856 poll); la
  migrazione a runqueue per-CPU resta lavoro futuro.
- Le syscall transazionali su lease fisiche (`SYS_SPAWN`, `SYS_DISPLAY_ACQUIRE`,
  `SYS_SHM_*`, `SYS_PROCESS_STOP`) disabilitano la sospensione syscall per
  mantenere coerente il budget fisico.
- GUI e creazione dei suoi processi operano sul BSP; input PS/2 e rendering
  software (heap Slint 4 MiB). Gli AP mantengono i percorsi IPC supportati.
- L'uptime parte dalla prima richiesta display del kernel e accumula i wrap
  ACPI PM. Ogni turno utente ha un budget massimo compatibile con mezzo
  periodo del contatore PM a 24 bit.
- Heap kernel 32 MiB, 256 lease fisiche e 512 extent. Il primo init resta
  incorporato; i programmi successivi vengono caricati dal VFS.
- La vista utente conserva le mapping supervisor minime per le transizioni.
  Le mitigazioni speculative dipendono dalle capability esposte dalla CPU.
- Dubbio accettato il 15 novembre 2026: in 1 sessione su 21 con tre QEMU
  paralleli un AP non ha confermato lo shootdown TLB entro 1 s (`K4TLBACK`).
  Causa ignota; nove ripetizioni diagnostiche non lo hanno riprodotto.

## Prove e prossimo passo

Verifiche correnti sull'immagine di produzione (`540cc38`, x2APIC attivo):
32/32 test host (`meson test`), 10/10 boot `--ordinary-shell --smp 4` in
x2APIC (`qemu-runner-18ebaa942622ceb4-b9a4`, `KERNEL_LAPIC_MODE: X2APIC`),
1/1 boot multi-app `--ordinary-gui 1 --smp 4` in x2APIC
(`qemu-runner-18ebaad61d1e2a74-9408`), 3/3 boot fallback xAPIC
(`-cpu max,-x2apic`, `KERNEL_LAPIC_MODE: XAPIC`), 3/3 boot WHPX headless in
x2APIC (`KERNEL_LAPIC_MODE: X2APIC`), e 1/1 soak continuo `--ordinary-soak 30
--smp 4` (`qemu-runner-18eba603e3ccafc4-2fc8`, 30+30 min, 248 round, 0 overruns,
0 perdite di memoria).
Validazioni in [gui.md](validation/gui.md),
[consolidamento.md](validation/consolidamento.md),
[processi.md](validation/processi.md), [storage.md](validation/storage.md),
[exec.md](validation/exec.md) e [sicurezza.md](validation/sicurezza.md).
Prossimo passo: Milestone 7 — USB xHCI + HID (tastiera/mouse USB) e avvio su
PC fisico.
