# Validazione dell'Obiettivo 5 — Sicurezza di sistema e cancello «base solida» (23 novembre 2026)

## Perimetro e immagine verificata

- **Profilo:** Meson `ordinary` (immagine unica di produzione).
- **Kernel ELF (`kernel.elf`):** SHA-256 `1BF874EE7BDC1909C8F182B8351C7EF8FB4CE0CCA5028FB8E13F44A9B1C75575`.
- **Loader UEFI (`BOOTX64.EFI`):** SHA-256 `8253E1DBC4BD6909A5F6EB3026C6C50C2964BA215DA8891A4243B22FECC65B1C`.
- **Macchina virtuale:** QEMU 11.1.0 (`q35`, TCG, `-cpu max`, OVMF `edk2-x86_64-code.fd`), 4 e 8 vCPU.

## Protezioni attive e provate sull'immagine `ordinary`

1. **Entropia hardware + Health Test + CSPRNG ([entropy.c](../../arch/x86/kernel/security/entropy.c), [random.c](../../kernel/security/random.c), [csprng.rs](../../kernel/security/rust/csprng.rs)):**
   - Rilevamento CPUID `07h/0:EBX[18]` (`RDSEED`) e `01h:ECX[30]` (`RDRAND`) con retry limitati; `RDTSC` usato solo come nonce/personalizzazione.
   - Startup health test NIST SP 800-90B (`HealthMonitor`, RCT + APT su 1024 campioni `u64`) e CSPRNG `HmacDrbg` SHA-256 (NIST SP 800-90A Rev. 1) collegati al boot (`KERNEL_ENTROPY_SOURCE: 0x2` = `RDSEED`, `KERNEL_ENTROPY_READY`).
2. **Stack canary del kernel `/GS` ([canary.c](../../kernel/security/canary.c), [canary.asm](../../arch/x86/kernel/security/canary.asm)):**
   - Tutto il kernel C è compilato con `/GS /Gs9999999` ([main.rs](../../scripts/tooling/build-plan/rust/plan/main.rs)).
   - `__security_cookie` viene randomizzato a 48 bit dal CSPRNG in `kernel_continue()` prima di `kernel_startup_platform` e `kernel_startup_devices` (`KERNEL_CANARY_READY`).
   - `__security_check_cookie` in Assembly preserva `RAX` secondo ABI MSVC x64 e panica con `STACK_COOKIE_CORRUPTED` su mismatch.
3. **Audit W^X runtime e guard page ([audit.c](../../arch/x86/kernel/security/audit.c)):**
   - Scansione di tutte le PTE dell'immagine kernel (`X86_KERNEL_IMAGE_BASE..X86_KERNEL_IMAGE_LIMIT`): **117 pagine RX** (`0x75`), **33 pagine RO+NX** (`0x21`), **279 pagine RW+NX** (`0x117`), **0 pagine W+X**, **0 pagine con bit `U/S=1`** (`KERNEL_WX_AUDIT_OK`).
   - Verifica sulle PTE delle guard page non presenti (`!PTE_PRESENT`) di stack principale (inferiore e superiore), stack IST e stack syscall/RSP0 (inferiore e superiore), con pagine stack `RW+NX` supervisor-only (`KERNEL_GUARD_AUDIT_OK`).
4. **Protezioni CPU (`NX`, `WP`, `SMEP`, `SMAP`, `UMIP`):**
   - `CR4` verificato su tutte le vCPU in ogni boot (`SMEP | SMAP | UMIP`).
   - Prova reale in Ring 3: `/bin/init.elf` spawna un figlio (`mode = 3`) che esegue `SGDT`; con `CR4.UMIP` attivo la CPU solleva `#GP(0)` (vettore `13`, confermato sia via `SYS_WAIT_PID` sia nel trace QEMU `("0d", "0000")`).
5. **KPTI (isolamento heap kernel da `UserRoot`) e audit mitigazioni speculative ([transition.asm](../../arch/x86/kernel/process/assembly/entry/transition.asm), [image.c](../../arch/x86/kernel/process/image.c), [audit.c](../../arch/x86/kernel/security/audit.c)):**
   - `x86_process_enter` e `x86_process_resume` passano sullo stack trampolino `gs:[GS_SYSCALL_TOP]` prima di caricare `UserRoot`; `X86_LAYOUT_HEAP_SLOT` (32 MiB heap kernel) non è mappato in `UserRoot` (`PML4[486] = 0`, `KERNEL_KPTI_HEAP_UNMAPPED: 0x1`).
   - Audit CPUID `07h/0:EDX` e `IA32_ARCH_CAPABILITIES` (`0x10A`) / `IA32_SPEC_CTRL` (`0x48`) eseguito al boot (`KERNEL_SPEC_AUDIT_OK`).

## Suite host (Meson, MSVC ASan + Rust)

| Suite host | Oggetto della prova | Esito |
| --- | --- | --- |
| `security_csprng_rust` | Health test RCT/APT (accetta campioni variati, rifiuta bloccati/ripetitivi), `HmacDrbg` instantiate/generate/reseed/wipe | 2/2 PASS |
| `security_random_host` | Rifiuto sorgente assente e RNG bloccato, `HmacDrbg` + reseed, `kernel_security_cookie_init` + panic `STACK_COOKIE_CORRUPTED` su cookie corrotto (MSVC ASan) | PASS |
| `process_table_host` | Crescita dinamica processi/IPC, isolamento, non-escalation budget, fault cleanup, zero perdite heap | PASS |
| `vfs_fat_host` | Mount FAT32/FAT16, fault-injection I/O su `truncate`/`unlink` (directory prima della FAT), `IF=1` durante tutto l'I/O, zero perdite heap | PASS |
| `block_cache_host` | Cache 4 KiB LRU write-through con `IoBusy` a `IF=1`, zero perdite heap | PASS |
| `libvoid_rust` | ABI `SpawnBudget`/`SpawnArgs`/`WaitStatus`, token splitting, formattazione | 3/3 PASS |
| `process_policy_rust` | Politiche syscall, `MAX_IMAGE_PAGES = 128`, `build_with_limit` | 14/14 PASS |
| `elf_parser_rust` | Validazione ELF64, W^X stretto, mutazioni bit-level | 9/9 PASS |
| `shell_command_rust` | Classificazione comandi inclusi `print`, `fl`, `rp` e rifiuto vecchi nomi | 4/4 PASS |
| `qemu_runner_unit` | Costruzione `vblk.img` FAT32, argomenti QEMU, parser seriale e gate | 21/21 PASS |

## Prove QEMU e cancello «base solida» pre-GUI

1. **`--ordinary-shell --smp 4 --boots 10`:** **10/10 PASS** (`kernel/build/qemu-runner-18ea9ddfe22ec1d4-3f78`).
2. **`--ordinary-shell --smp 8 --boots 3`:** **3/3 PASS** (`kernel/build/qemu-runner-18ea9e11f1340f18-6668`).
3. **`--ordinary-no-ps2 --smp 4 --boots 1`:** **1/1 PASS** (`kernel/build/qemu-runner-18ea9e24f3a01978-657c`).
4. **Soak continuo da 30 minuti (`--ordinary-soak 30 --smp 4`):**
   - Eseguito per 30 minuti pieni sull'immagine `1BF874EE...` (`kernel/build/qemu-runner-18ea9e294bd3b858-6200`): **499 comandi** (`249` round `bridge status` + `249` job `ap 100000000` su 4 CPU con migrazione + `exit`), **90.384 tick di preemption**, **1.236 sospensioni preemptive**, **0 overrun**, **0 errori/resync/discard PS/2** su 22.425 byte, `KERNEL_DISPATCH_FREE_PAGES: 0x2e2f` (= 11.823 pagine stabili, 0 leak), chiusura pulita (`KERNEL_PS2_INPUT_CLEANUP_OK`, `KERNEL_READY`, `KERNEL_DISPATCHER_READY`). Le uniche 2 eccezioni nell'intero `qemu.log` sono i 2 test intenzionali di `/bin/init.elf` al boot (`0x6` `#UD` e `0xd` `#GP` UMIP).
   - Dopo l'aggiornamento del check fault di `soak_boot` in [ordinary.rs](../../scripts/tooling/qemu-runner/rust/scenario/ordinary.rs#L692) per attendere esattamente `[("06","0000"), ("0d","0000")]`, verifica del runner su `--ordinary-soak 1 --boots 1 --smp 4`: **1/1 PASS** (`kernel/build/qemu-runner-18ea9fe0485cffc8-638`).

## Limiti noti e cosa non è stato provato

- **Hardware fisico:** non provato su PC fisico (provato solo su QEMU 11.1.0 TCG/q35).
- **Trampolino KPTI:** `X86_LAYOUT_HEAP_SLOT` (32 MiB), `RECURSIVE_SLOT`, `FB..IOMMU` e `DMA_SLOT` non sono mappati in `UserRoot`; restano mappati con `U/S=0` `SLOT_IMAGE`, `WINDOWS_SLOT` e `AP_SLOT` per il codice/stato di transizione, GDT/TSS/IDT e stack syscall/IST. Isolare `.text.trampoline` su pagina dedicata resta un miglioramento futuro.
- **Mitigazioni speculative hardware:** QEMU TCG `-cpu max` espone `KERNEL_SPEC_CPUID_EDX = 0x10` (senza `IBRS`/`STIBP`/`IA32_ARCH_CAPABILITIES`); l'abilitazione di `IA32_SPEC_CTRL` è provabile solo su CPU fisica o host con tali bit.
- **Overflow reale dello stack syscall:** le guard page sono verificate non presenti sulle PTE, ma un overflow intenzionale dallo stack syscall in produzione non è stato iniettato.
