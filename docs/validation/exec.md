# Validazione dell'Obiettivo 4 — Esecuzione da file (22 novembre 2026)

## Perimetro e immagine verificata

- **Profilo:** Meson `ordinary` (immagine unica di produzione).
- **Kernel ELF (`kernel.elf`):** SHA-256 `51624CE3FCCF477D5E160BE1F3860A183909ED566CCF632CE20A65392D70C4BE`.
- **Loader UEFI (`BOOTX64.EFI`):** SHA-256 `8253E1DBC4BD6909A5F6EB3026C6C50C2964BA215DA8891A4243B22FECC65B1C`.
- **Binari utente su `/bin/` (VirtIO-blk FAT32, 2048 settori = 1 MiB):**
  - `/bin/init.elf` (`user-init.elf`, 9.392 byte)
  - `/bin/bridge.elf` (`user-bridge.elf`, 11.272 byte)
  - `/bin/worker.elf` (`user-worker.elf`, 9.064 byte)
  - `/bin/sh.elf` (`user-sh.elf`, 17.240 byte)
- **Macchina virtuale:** QEMU 11.1.0 (`q35`, TCG, OVMF `edk2-x86_64-code.fd`), 4 e 8 vCPU.

## Suite host (Meson, MSVC ASan + Rust)

| Suite host | Oggetto della prova | Esito |
| --- | --- | --- |
| `process_table_host` | Crescita dinamica processi/IPC, isolamento handle, fault cleanup, `KERNEL_PROCESS_BUDGET` e regola di non-escalation (`MaxImagePages`, `MaxOpenFiles`, `CpuBudgetMs`, flag `SPAWN`), zero perdite heap | PASS |
| `vfs_fat_host` | Mount VFS, `stat`, `readdir`, lettura/scrittura/unlink FAT32/FAT16, fault-injection I/O su `truncate`/`unlink` (directory aggiornata prima della FAT), verifica `IF=1` durante tutto l'I/O VFS/cache, zero perdite heap | PASS |
| `block_cache_host` | Cache a blocchi 4 KiB LRU write-through con `IoBusy` a `IF=1`, zero perdite heap | PASS |
| `libvoid_rust` | Dimensioni ABI (`SpawnBudget` 16 B, `SpawnArgs` 32 B, `WaitStatus` 24 B), `split_first_token`, `format_u64` | 3/3 PASS |
| `process_policy_rust` | Politiche syscall, `MAX_IMAGE_PAGES = 128` (512 KiB), `build_with_limit` (64 e 128 pagine accettate, superamento budget rifiutato) | 14/14 PASS |
| `elf_parser_rust` | Validazione ELF64, W^X stretto, mutazioni bit-level e troncamenti su ELF reali | 9/9 PASS |
| `shell_command_rust` | Classificazione comandi inclusi `print`, `fl`, `rp` e rifiuto dei vecchi nomi `echo`, `ls`, `cat` | 4/4 PASS |
| `qemu_runner_unit` | Costruzione `vblk.img` FAT32 con `/bin/*.elf`, argomenti QEMU, parser seriale e gate di sessione | 21/21 PASS |

## Prove di boot sull'immagine di produzione (`ordinary`)

1. **`--ordinary-shell --smp 4 --boots 10`:** **10/10 PASS** (`kernel/build/qemu-runner-18ea7c97912de2d8-a218`).
   - `/bin/init.elf` caricato dal VFS esegue in Ring 3:
     - `SYS_SPAWN` di un figlio sandboxed (`/bin/init.elf` mode 2, budget ristretto con `SPAWN` revocato) che verifica il rifiuto di `SYS_SPAWN`, legge `/HELLO.TXT`, stampa `USER: VoidOS child ok` ed esce con codice `42`, raccolto via `SYS_WAIT_PID`.
     - `SYS_SPAWN` di un figlio ostile (`/bin/worker.elf` arg `0`, `#UD` CPL3), raccolto via `SYS_WAIT_PID` con `EXIT_KIND_FAULT` e `fault_vector == 6` senza fermare il sistema.
     - Rifiuto di escalation budget (`max_image_pages = 999`).
     - Zero pagine fisiche perse (`KERNEL_VFS_EXEC_OK`).
   - `/bin/sh.elf` caricato dal VFS esegue in Ring 3 via `SYS_CONSOLE_READ`, VFS e `SYS_SPAWN`/`SYS_WAIT_PID`:
     - `print sh_ok`, `fl /`, `write shnote ok`, `rp /SHNOTE`, `rm /SHNOTE`, `sync`, `run /bin/init.elf`, `exit` (`USER: VoidOS sh exit`, zero pagine fisiche perse, `KERNEL_USER_SHELL_OK`).
   - Sessione interattiva PS/2: `/bin/bridge.elf` residente e `proc` (5 processi da `/bin/`, inclusi IPC cross-CPU AP1↔BSP e verifiche syscall `11..15` da `worker.elf`), `fl`, `write`, `rp`, `rm`, `sync`, `ap 1000000 auto 16`, `exit` senza leak.
2. **`--ordinary-shell --smp 8 --boots 3`:** **3/3 PASS** (`kernel/build/qemu-runner-18ea7cd620baa574-aba8`).
3. **`--ordinary-no-ps2 --smp 4 --boots 1`:** **1/1 PASS** (`kernel/build/qemu-runner-18ea7cea64d98a04-46e0`), inclusi `KERNEL_VFS_EXEC_OK` e `KERNEL_USER_SHELL_OK` senza controller PS/2.

## Limiti noti e cosa non è stato provato

- **Hardware fisico:** non provato su PC fisico (provato solo su QEMU 11.1.0 TCG/q35).
- **Boot precoce pre-VFS:** [boot.c](../../kernel/process/boot.c) (`kernel_process_boot`) usa ancora il primo `init` incorporato perché gira prima dell'inizializzazione di VirtIO-blk/VFS; tutti i processi successivi al mount VFS (`/bin/init.elf`, `/bin/sh.elf`, `/bin/bridge.elf`, `/bin/worker.elf`) sono caricati da disco.
- **Spazio utente:** limite immagine di 128 pagine (512 KiB) in una singola PT utente; nessun heap dinamico utente (`mmap`/`brk`) in questa milestone.
- **Console interattiva PS/2:** la shell kernel resta attiva come console interattiva di sistema/fallback, mentre `/bin/sh.elf` usa `SYS_CONSOLE_READ` sulla coda input del kernel.
