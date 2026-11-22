# Stato di VoidOS — 22 novembre 2026

## Immagine ordinaria (`ordinary`, Meson)

- Il loader UEFI carica il kernel ELF. Il kernel configura paging con NX e
  WP, GDT/TSS/IST, allocatore fisico, heap e interrupt. In QEMU è stato
  inizializzato anche il framebuffer GOP.
- In QEMU con quattro vCPU, il core di avvio (BSP) avvia tre core secondari
  (AP). Ogni AP entra in un dispatcher permanente e risponde al PING
  inter-CPU. Il dispatcher persiste anche senza PS/2 e dopo `exit`.
  BSP e AP usano IDT e ingresso IRQ comuni, con sottoscrizioni per CPU.
  Lo scheduler gestisce la sessione PS/2 sul BSP con un timer di preemption
  da 10 ms (vettore D1h). I task della sessione partono con gli interrupt
  attivi e il timer li sospende anche fuori dalla finestra di attesa.
- Con SMEP e SMAP disponibili, il runtime Ring 3 è permanente sull'IDT comune
  su BSP e AP con stato di transizione per-CPU (`gs:`), stack syscall/RSP0
  dedicato con guard page su ogni AP (33 pagine mappate per AP), `SYSCALL`
  attivo stabilmente su tutte le CPU, syscall interrompibili (`IF=1` fuori
  dalle sezioni critiche minime) e tabelle dinamiche di processi, handle e
  canali IPC su `kernel_heap()` (fino a 256). Al boot `init.elf` gira e viene
  raccolto tramite `x86_process_manager`. UMIP viene abilitato se la CPU lo
  supporta. Lo stack delle syscall ha due guard page coperte dall'inventario
  paging.
- Al boot il kernel avvia due driver a blocchi reali sull'immagine ordinaria:
  VirtIO-blk PCI modern (OASIS VirtIO 1.2, `00:02.0`, 128 settori) e AHCI SATA
  (Intel AHCI 1.3.1, controller ICH9 `00:1f.2` porta 0, `IDENTIFY`,
  `READ/WRITE DMA EXT`, `FLUSH CACHE EXT`), ciascuno servito da una cache a
  blocchi LRU write-through da 64 linee × 4 KiB su `kernel_heap()`. Il VFS
  monta `/` sul volume FAT32 VirtIO-blk e `/boot` sulla ESP FAT16 AHCI SATA,
  verifica in lettura `/HELLO.TXT` (21 byte) e `/boot/EFI/BOOT/boot-check.txt`
  (15 byte via LFN), ed esegue un ciclo di scrittura, flush, invalidazione
  cache, rilettura e rimozione su `/VERIFY.TXT`. I processi Ring 3 dispongono
  delle syscall `SYS_OPEN (6)`, `SYS_READ (7)`, `SYS_WRITE_FILE (8)`,
  `SYS_CLOSE (9)` e `SYS_UNLINK (10)` con tabella dinamica dei file aperti
  (fino a 256) e chiusura automatica alla terminazione del processo.
- Con PS/2, `ordinary` apre la shell con tastiera e mouse. `bridge status`
  interroga il servizio residente come normale processo gestito senza pausare
  lo scheduler; `proc` avvia 5 processi isolati contemporanei (6 con `bridge`:
  `bridge.elf` server eseguito su AP1 in Ring 3 con IPC cross-CPU verso
  `worker.elf` sul BSP che legge anche `/HELLO.TXT` dal VFS via syscall
  `SYS_OPEN`/`SYS_READ`/`SYS_CLOSE`, seconda coppia server/client utente↔utente,
  e 1 fault CPL3 `#UD` isolato e raccolto senza perdite); `ls`, `cat`, `write`,
  `rm` e `sync` operano direttamente sui filesystem FAT32/FAT16 montati nel
  VFS; `ap <n> [cpu|auto] [tasks]` esegue job fidati sugli AP e sul BSP. `exit`
  chiude la sessione e rilascia processi, task, stack, IRQ e binding PS/2. Il
  dispatcher BSP continua con timer LAPIC e attese HPET, anche senza controller
  PS/2. [Build e boot](../README.md).

## Profili separati

| Profilo o caso | Capacità provata |
| --- | --- |
| `v2c` | Console VirtIO con un dominio VT-d per la GPU, in un profilo separato. |
| `meson-bsp-periodic`, `ap-task-four` | Preemption da timer sul BSP nel primo caso. Nel secondo, otto task kernel fidati distribuiti su quattro AP, con affinità fissa. |
| `k6-process`, `k6-process-smp` | Casi diagnostici di esecuzione in ring 3, fault utente e IPC tra due processi, con cleanup delle risorse. |

Le configurazioni sono nel [registro delle campagne](../scripts/test/campaigns.json).
I profili diagnostici compilati solo da `build.ps1` saranno ritirati man mano
che le loro prove trovano posto nei test host o nell'immagine unica.

## Limiti e prossimo passo

- La 3e chiude le prove della preemption nella sessione PS/2 sul BSP.
  B2 chiude il [dispatcher permanente sul BSP](design/scheduler-produzione.md#prossimo-passo-di-s1)
  in `ordinary`, con cleanup verificato della sessione e continuità senza PS/2.
  Runtime AP permanente e preemption AP sono fatti. Il timer one-shot
  drena il vecchio tick prima del riarmo. I job sono a IF=1.
  Il budget lock è un secondo senza progresso, con cap di un
  milione di poll. La task 5 (selezione senza affinità fissa e migrazione
  tra le CPU ammesse) è fatta; il rendering della console spezza la sezione
  a interrupt spenti per singola cella, azzerando gli overflow della FIFO
  PS/2 sotto carico. Il limite fisso a otto task è stato rimosso: il core
  Rust e il backend x86 supportano 64 task (fino a 256 nel core Rust) con
  tabelle allocate su `kernel_heap()` e quattro PT dedicate per gli stack
  con guard page nello slot heap (`ap <n> [cpu|auto] [tasks]`). Il registro
  IRQ copre 222 vettori sottoscrivibili per CPU.
  I record IRQ delle 64 CPU del bootstrap riservano 568 KiB nell'immagine.
  L'heap è configurato a 32 MiB (`KERNEL_HEAP_MAX_PAGES = 8192`, 512 pagine
  slab = 2 MiB, 22 tabelle paging con guard page e 4 PT per 64 task stack).
  L'allocatore fisico supporta 256 lease (`KERNEL_PHYSICAL_LEASES = 256`) e
  512 extent. `kernel_text` su COM1 non ha lock SMP: i processi utente su AP
  usano solo syscall SMP-safe (`SYS_WAIT_READABLE`, `SYS_RECV`, `SYS_SEND`,
  `SYS_EXIT`).
- Una porzione di audit a IRQ spenti ha raggiunto 18,7 ms nella 3d-4
  e circa 13,5 ms nel soak della 3e. Il quanto nominale è 10 ms.
  [Validazione](validation/consolidamento.md#cpu-e-scheduler).
- Mancano `exec` di ELF caricati dal VFS, `init` e shell in Ring 3
  (Obiettivo 4: i programmi utente sono ancora incorporati nell'immagine). La
  scrittura FAT (`kernel_vfs_write_file`) e `unlink` operano su file 8.3 nella
  directory radice del mount (la lettura supporta percorsi annidati e LFN).
- Mancano sorgente di entropia e stack canary del kernel. W^X per sezione
  dell'intera immagine e overflow reale dello stack syscall sono non
  verificati. Il cancello «base solida» resta aperto. Hardware fisico non
  verificato per il percorso descritto qui.
- Il watchdog HPET rileva e recupera il confronto saltato dall'HPET di
  QEMU 11.1 (circa una volta su un milione di armamenti); recupero osservato
  dal vivo in una sessione da 30 minuti.
- Dubbio aperto, accettato dall'utente il 15 novembre 2026: in 1 sessione da
  30 minuti su 21 (tre QEMU in parallelo), dopo la chiusura della shell, un
  AP non ha confermato lo shootdown del TLB entro 1 s (`K4TLBACK`). Causa
  ignota; ipotesi non verificata: carico dell'host. Non si è ripresentato in
  9 sessioni con diagnostica dedicata.
- Il backend processi è stato diviso. La CI ha un workflow per suite host
  e boot ordinario. Le suite host principali (`clock`, `sync`, `scheduler`,
  `line_editor`, `shell_command_rust`, `elf_parser`, `process_policy`,
  `user_protection`, `watchdog_integration`, `log_boundary`,
  `process_table_host`, `physical_allocator_host`, `heap_host`,
  `block_cache_host`, `virtio_blk_protocol_rust`, `ahci_protocol_rust`,
  `vfs_fat_rust`, `vfs_fat_host`, 26 test C+ASan e Rust) girano ora in un
  unico [test/meson.build](../test/meson.build) senza dipendere da
  PowerShell 7; restano negli script PowerShell le suite legacy dei driver
  diagnostici.

## Evidenze

Obiettivo 3 (Storage: VirtIO-blk, AHCI SATA, cache a blocchi LRU write-through,
VFS FAT32/FAT16 in lettura/scrittura, syscall Ring 3 e comandi shell), Meson
`ordinary`, kernel SHA-256
`B7F0B709201E62ADA618046CAF0851358592874D2A6941A7967267E35508B3C3`,
loader SHA-256 `8253E1DBC4BD6909A5F6EB3026C6C50C2964BA215DA8891A4243B22FECC65B1C`.
QEMU/OVMF TCG/q35: 10/10 boot `--ordinary-shell --smp 4` (`FreePages: 11834`
stabili, `Overruns: 0`), 3/3 boot `--ordinary-shell --smp 8` (`FreePages:
11606` stabili, `Overruns: 0`), 1/1 boot `--ordinary-no-ps2 --smp 4`
(`FreePages: 11837`). Su ogni boot: mount `/` su VirtIO-blk FAT32 e `/boot` su
AHCI SATA ESP FAT16 (`KERNEL_VFS_MOUNTS: 0x2`, `KERNEL_VFS_ROOT_BYTES: 0x15`,
`KERNEL_VFS_BOOT_BYTES: 0xf`, `KERNEL_VFS_READY`), `proc` con 6 processi Ring 3
simultanei (`bridge.elf` su AP1 con IPC cross-CPU verso `worker.elf` su BSP che
apre, legge 21 byte e chiude `/HELLO.TXT` via `SYS_OPEN`/`SYS_READ`/`SYS_CLOSE`,
seconda coppia IPC utente↔utente, 1 `#UD` CPL3 isolato e raccolto), comandi
shell `ls`, `write note voidos`, `cat note`, `rm note`, `sync`, e
`ap 1000000 auto 16`. Test host con ASan: `block_cache_host` PASS,
`vfs_fat_host` PASS, `virtio_blk_protocol_rust` **3/3 PASS**,
`ahci_protocol_rust` **2/2 PASS**, `vfs_fat_rust` **2/2 PASS**,
`shell_command_rust` **4/4 PASS**, `qemu_runner_unit` **21/21 PASS** (26 test
in `test/meson.build`). Hardware fisico non verificato.
[Validazione storage](validation/storage.md),
[validazione processi](validation/processi.md).

Risultati precedenti con immagini e limiti propri nelle validazioni
[AP nel boot ordinario](validation/consolidamento.md#validazione-dei-backend-x86),
[scheduler e shell](../kernel/scheduler/documentation/validation.md),
[consolidamento e build Meson](validation/consolidamento.md), [processi](validation/processi.md), [storage](validation/storage.md) e [protezioni](validation/sicurezza.md).
L'elenco storico dei checkpoint è nella versione di questa pagina a `efecb8b`.
