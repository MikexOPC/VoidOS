# Stato di VoidOS — 26 novembre 2026

## Immagine di produzione

`ordinary`, compilata con Meson e Ninja, avvia il loader UEFI, il kernel,
init e la shell. Le verifiche attuali sono su QEMU/OVMF q35 TCG.

- BSP e AP hanno dispatcher permanenti, IRQ comuni e preemption. La sessione
  PS/2 possiede e rilascia task, stack, IRQ e binding. Il BSP resta attivo
  dopo `exit` e senza PS/2. Task e affinità usano tabelle dinamiche, con
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

**Desktop con app reali (Terminale + System Monitor) — fatta (su QEMU TCG).**
System Monitor gira come processo Ring 3 reale dal dock (`kind == 1`), legge
metriche vere del kernel tramite `SYS_SYS_INFO` (`28`: RAM fisica, heap kernel,
CPU online via `x86_percpu_online_count()`, processi attivi, canali IPC, uptime,
preempt ticks) e gira affiancato al Terminale senza sovrapposizione. Risolti
durante la verifica multi-app: isolamento atomico di `cpu->CopyScratch` in
`copy_checked` sotto preemption syscall, aggiornamento in-place dei pixel in
Slint (`set_row_data` senza distruggere i `TouchArea` delle altre finestre) e
sincronizzazione immediata di `close-clicked`.
Restano: test di stress continuo da 30 minuti, prova interattiva WHPX/SDL e
hardware fisico. L'ingresso interattivo è `gui`.

## Limiti noti

- Hardware fisico, USB xHCI, x2APIC e rete non sono ancora stati provati o
  implementati. Il test di stress continuo da 30 minuti non è stato eseguito
  su questa revisione.
- Lo scheduler usa ancora un ticket lock condiviso (rank 900). La contesa
  misurata su 4 CPU in boot ordinario è 0,45% (`2.138 / 471.736`), media 83
  poll, picco 8.625 poll; la migrazione a runqueue per-CPU resta lavoro futuro.
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

Verifiche correnti sull'immagine di produzione (`3622E952...`): 32/32 test
host (`meson test`), 10/10 boot `--ordinary-shell --smp 4`
(`qemu-runner-18eba47a1f1e0cfc-59f8`), 3/3 boot multi-app `--ordinary-gui 1
--smp 4` (`qemu-runner-18eba439b540567c-5464`, 21 cicli, 63 avvii app — 42
Terminale + 21 System Monitor — 21 comandi VFS, 0 pixel residui cursore,
screenshot ispezionato visivamente con entrambe le app e `CPUS: 4`).
Validazioni in [gui.md](validation/gui.md),
[consolidamento.md](validation/consolidamento.md),
[processi.md](validation/processi.md), [storage.md](validation/storage.md),
[exec.md](validation/exec.md) e [sicurezza.md](validation/sicurezza.md).
Prossimo passo: stress da 30 minuti (`--ordinary-soak 30`) e prova su hardware
fisico / periferiche (xHCI USB, x2APIC).
