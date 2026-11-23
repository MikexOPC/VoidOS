# Stato di VoidOS — 23 novembre 2026

## Immagine ordinaria (`ordinary`, Meson)

- Il loader UEFI carica il kernel ELF. Il kernel configura paging con NX e
  WP, GDT/TSS/IST, entropia hardware (`RDSEED`/`RDRAND` via CPUID + health test
  NIST SP 800-90B + `HMAC_DRBG` SHA-256), stack canary `/GS` su tutto il codice
  C del kernel con `__security_cookie` casuale a 48 bit al boot, audit W^X
  runtime su tutte le PTE dell'immagine kernel (117 RX, 33 RO+NX, 279 RW+NX,
  0 W+X) e sulle guard page degli stack, allocatore fisico, heap e interrupt.
  In QEMU è stato inizializzato anche il framebuffer GOP.
- In QEMU con quattro vCPU, il core di avvio (BSP) avvia tre core secondari
  (AP). Ogni AP entra in un dispatcher permanente e risponde al PING
  inter-CPU. Il dispatcher persiste anche senza PS/2 e dopo `exit`.
  BSP e AP usano IDT e ingresso IRQ comuni, con sottoscrizioni per CPU.
  Lo scheduler gestisce la sessione PS/2 sul BSP con un timer di preemption
  da 10 ms (vettore D1h). I task della sessione partono con gli interrupt
  attivi e il timer li sospende anche fuori dalla finestra di attesa.
- Con SMEP e SMAP disponibili, il runtime Ring 3 è permanente sull'IDT comune
  su BSP e AP con stato di transizione per-CPU (`gs:`), stack syscall/RSP0
  dedicato con guard page su ogni AP (33 pagine mappate per AP), KPTI con
  rimozione dell'intero heap kernel (`X86_LAYOUT_HEAP_SLOT`, 32 MiB) dalla PML4
  utente `UserRoot` e switch sullo stack trampolino in `transition.asm`, audit
  `CPUID`/`IA32_ARCH_CAPABILITIES`, `SYSCALL` attivo stabilmente su tutte le
  CPU, syscall interrompibili (`IF=1` fuori dalle sezioni critiche minime) e
  tabelle dinamiche di processi, handle e canali IPC su `kernel_heap()` (fino
  a 256). UMIP viene abilitato se la CPU lo supporta e verificato in Ring 3
  tramite `SGDT` → `#GP(0)` (vettore `13`).
- Al boot il kernel avvia due driver a blocchi reali sull'immagine ordinaria:
  VirtIO-blk PCI modern (OASIS VirtIO 1.2, `00:02.0`, 2048 settori = 1 MiB) e
  AHCI SATA (Intel AHCI 1.3.1, controller ICH9 `00:1f.2` porta 0, `IDENTIFY`,
  `READ/WRITE DMA EXT`, `FLUSH CACHE EXT`), ciascuno servito da una cache a
  blocchi LRU write-through da 64 linee × 4 KiB su `kernel_heap()`. Sia il VFS
  sia la block cache tengono gli interrupt attivi (`IF=1`) durante gli
  attraversamenti FAT e l'I/O sincrono su disco (mutua esclusione via `IoBusy`
  con spinlock breve). Il VFS monta `/` sul volume FAT32 VirtIO-blk e `/boot`
  sulla ESP FAT16 AHCI SATA, verifica in lettura `/HELLO.TXT` (21 byte) e
  `/boot/EFI/BOOT/boot-check.txt` (15 byte via LFN), esegue un ciclo di
  scrittura/sync/rilettura/unlink su `/VERIFY.TXT` (aggiornando la directory
  prima di liberare i cluster FAT in `truncate` e `unlink`), esegue
  `/bin/init.elf` dal VFS (`KERNEL_VFS_EXEC_OK`: `SYS_SPAWN` + `SYS_WAIT_PID`
  su figlio sandboxed con `SPAWN` revocato, figlio `#UD` raccolto con
  `EXIT_KIND_FAULT` e rifiuto escalation budget) e avvia la shell utente
  `/bin/sh.elf` in Ring 3 (`KERNEL_USER_SHELL_OK`: `print`, `fl`, `write`,
  `rp`, `rm`, `sync`, `run /bin/init.elf`, `exit` via `SYS_CONSOLE_READ` e
  `libvoid`). I processi Ring 3 usano `libvoid` (`userspace/lib/libvoid.rs`,
  crate `.rlib`) sulle syscall `0..15` (`SYS_EXIT`, `SYS_WRITE`, `SYS_YIELD`,
  IPC `3..5`, VFS `6..10`, `SYS_SPAWN (11)`, `SYS_WAIT_PID (12)`,
  `SYS_LIST_DIR (13)`, `SYS_SYNC (14)`, `SYS_CONSOLE_READ (15)`) con budget
  risorse per-processo (`KERNEL_PROCESS_BUDGET`: `MaxImagePages` fino a 128 =
  512 KiB, `MaxOpenFiles`, `CpuBudgetMs`, non-escalation) e chiusura
  automatica dei file alla terminazione. `/bin/bridge.elf` e `/bin/worker.elf`
  nella sessione ordinaria vengono caricati da `/bin/` sul VFS.
- Con PS/2, `ordinary` apre la shell con tastiera e mouse (`help`, `print`,
  `status`, `bridge status`, `proc`, `fl`, `rp`, `write`, `rm`, `sync`,
  `ap <n> [cpu|auto] [tasks]`, `exit`). `bridge status` interroga il servizio
  residente `/bin/bridge.elf` senza pausare lo scheduler; `proc` avvia 5
  processi isolati da `/bin/` (6 con `bridge`: `bridge.elf` su AP1 con IPC
  cross-CPU verso `worker.elf` sul BSP che esercita VFS e syscall `11..15`,
  seconda coppia utente↔utente, e 1 fault CPL3 `#UD` isolato e raccolto senza
  perdite). `exit` chiude la sessione e rilascia processi, task, stack, IRQ e
  binding PS/2. Il dispatcher BSP continua con timer LAPIC e attese HPET, anche
  senza controller PS/2. [Build e boot](../README.md).

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
- **Obiettivi 1–5 chiusi e cancello «base solida» prima della GUI superato.**
  Limiti noti accettati:
  - Hardware fisico non ancora verificato (tutte le prove su QEMU 11.1.0).
  - `kernel_process_boot` usa ancora il primo `init` incorporato prima del
    mount VFS; limite immagine utente a 128 pagine (512 KiB) senza `mmap`/`brk`
    dinamico; scrittura FAT (`kernel_vfs_write_file`) e `unlink` su nomi 8.3
    nella root del mount (lettura con sottodirectory e LFN).
  - KPTI rimuove l'intero heap kernel (32 MiB), vista ricorsiva e slot device
    da `UserRoot`; restano mappati con `U/S=0` `SLOT_IMAGE`, `WINDOWS_SLOT` e
    `AP_SLOT` per il percorso di transizione (isolamento `.text.trampoline` su
    pagina dedicata come miglioramento futuro). QEMU TCG `-cpu max` non espone
    `IBRS`/`STIBP`/`IA32_ARCH_CAPABILITIES`.
- Prossimo obiettivo della roadmap: **Obiettivo 6 — GUI** (`userspace/gui/ui/`
  per compositor/desktop, `userspace/gui/apps/` per applicazioni, comunicazione
  mediata dal servizio Ring 3 `/bin/bridge.elf`).
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

Obiettivo 5 (Sicurezza di sistema e chiusura cancello «base solida» pre-GUI:
entropia `RDSEED`/`RDRAND` + health test RCT/APT + `HMAC_DRBG`, stack canary
`/GS` con cookie casuale 48-bit al boot, audit W^X runtime 117 RX / 33 RO /
279 RW / 0 W+X e guard page, UMIP `SGDT` → `#GP` in Ring 3, KPTI con rimozione
`HEAP_SLOT` da `UserRoot`, audit CPUID/MSR mitigazioni speculative), Meson
`ordinary`, kernel SHA-256
`1BF874EE7BDC1909C8F182B8351C7EF8FB4CE0CCA5028FB8E13F44A9B1C75575`, loader
SHA-256 `8253E1DBC4BD6909A5F6EB3026C6C50C2964BA215DA8891A4243B22FECC65B1C`.
QEMU/OVMF TCG/q35: **10/10 boot** `--ordinary-shell --smp 4`
(`kernel/build/qemu-runner-18ea9ddfe22ec1d4-3f78`), **3/3 boot**
`--ordinary-shell --smp 8` (`kernel/build/qemu-runner-18ea9e11f1340f18-6668`),
**1/1 boot** `--ordinary-no-ps2 --smp 4`
(`kernel/build/qemu-runner-18ea9e24f3a01978-657c`), **soak continuo da 30
minuti** `--ordinary-soak 30 --smp 4` (`qemu-runner-18ea9e294bd3b858-6200`:
499 comandi, 90.384 tick preemption, 0 overrun, 0 errori PS/2, `FreePages:
11823` stabili) + verifica runner `--ordinary-soak 1 --boots 1` **1/1 PASS**
(`qemu-runner-18ea9fe0485cffc8-638`). Test host ASan/Rust:
`security_csprng_rust` **2/2 PASS**, `security_random_host` PASS,
`process_table_host` PASS, `vfs_fat_host` PASS, `block_cache_host` PASS,
`libvoid_rust` **3/3 PASS**, `process_policy_rust` **14/14 PASS**,
`elf_parser_rust` **9/9 PASS**, `shell_command_rust` **4/4 PASS**,
`qemu_runner_unit` **21/21 PASS**. Hardware fisico non verificato.
[Validazione sicurezza](validation/sicurezza.md),
[validazione esecuzione da file](validation/exec.md),
[validazione storage](validation/storage.md),
[validazione processi](validation/processi.md).

Risultati precedenti con immagini e limiti propri nelle validazioni
[AP nel boot ordinario](validation/consolidamento.md#validazione-dei-backend-x86),
[scheduler e shell](../kernel/scheduler/documentation/validation.md),
[consolidamento e build Meson](validation/consolidamento.md), [processi](validation/processi.md), [storage](validation/storage.md) e [protezioni](validation/sicurezza.md).
L'elenco storico dei checkpoint è nella versione di questa pagina a `efecb8b`.
