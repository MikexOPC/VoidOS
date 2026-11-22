# Validazione dell'Obiettivo 3 — Storage (22 novembre 2026)

## Perimetro e immagine verificata

- **Profilo:** Meson `ordinary` (immagine unica di produzione).
- **Revisione iniziale Obiettivo 3 (`464dd6b`):** `kernel.elf` SHA-256 `B7F0B709201E62ADA618046CAF0851358592874D2A6941A7967267E35508B3C3`.
- **Revisione corrente (fix sicurezza FAT + I/O VFS/cache a `IF=1` + `/bin/` su VirtIO-blk 1 MiB):** `kernel.elf` SHA-256 `679BBED6E6AD0DC14D18E3979982ED5B9F7F39F25829E43C730FFCD33BBC8D19`.
- **Loader UEFI (`BOOTX64.EFI`):** SHA-256 `8253E1DBC4BD6909A5F6EB3026C6C50C2964BA215DA8891A4243B22FECC65B1C`.
- **Macchina virtuale:** QEMU 11.1.0 (`q35`, TCG, OVMF `edk2-x86_64-code.fd`), disco ESP SATA AHCI (`00:1f.2`, FAT16, 1.032.192 settori = 504 MiB) + disco secondario VirtIO-blk PCI modern (`disable-legacy=on`, `00:02.0`, FAT32, 2048 settori = 1 MiB nella revisione corrente; 128 settori = 64 KiB a `464dd6b`).

## Suite host (Meson, MSVC ASan + Rust)

Tutte le suite di storage e di sistema in [test/meson.build](../../test/meson.build) sono state eseguite con warning come errori e AddressSanitizer abilitato sui binari C:

| Suite host | Oggetto della prova | Esito |
| --- | --- | --- |
| `block_cache_host` | Cache a blocchi da 4096 byte su `KERNEL_HEAP`, sostituzione LRU a 64 linee, write-through su `KERNEL_BLOCK_DEVICE`, letture/scritture multi-blocco non allineate, invalidazione su errore I/O, zero perdite heap | PASS |
| `virtio_blk_protocol_rust` | Validazione `no_std` capabilities PCI VirtIO 1.2 (`COMMON_CFG`, `NOTIFY_CFG`, `ISR_CFG`, `DEVICE_CFG`), negoziazione `VIRTIO_F_VERSION_1`, rifiuto di capability sovrapposte o cicliche | 3/3 PASS |
| `ahci_protocol_rust` | Validazione PCI AHCI 1.3.1 (`class 01:06:01`, ABAR BAR5), costruzione FIS Host-to-Device (`0x27`, `IDENTIFY 0xEC`, `READ DMA EXT 0x25`, `WRITE DMA EXT 0x35`, `FLUSH CACHE EXT 0xEA`) e parsing `IDENTIFY` LBA48 | 2/2 PASS |
| `vfs_fat_rust` | Rilevamento partizione MBR, validazione BPB FAT16/FAT32, decodifica catena cluster, entry 8.3 e VFAT LFN | 2/2 PASS |
| `vfs_fat_host` | Mount VFS (`/` e `/boot`), `stat`, `readdir`, lettura multi-cluster, creazione/scrittura/overwrite/unlink su FAT16 e FAT32 con persistenza su disco dopo invalidazione cache, tabella dinamica file aperti fino a 256, rilevamento cicli FAT, zero perdite heap | PASS |
| `shell_command_rust` | Classificazione token dei comandi shell inclusi `ls` (10), `cat` (11), `write` (12), `rm` (13), `sync` (14) e mutazioni sui confini | 4/4 PASS |
| `qemu_runner_unit` | Costruzione immagine FAT32 `vblk.img`, argomenti QEMU `-device virtio-blk-pci,drive=vblk0,disable-legacy=on`, parser seriale e gate di sessione | 21/21 PASS |

## Prove di boot sull'immagine di produzione (`ordinary`)

1. **`--ordinary-shell --smp 4 --boots 10`:** **10/10 PASS** (`kernel/build/qemu-runner-18ea767dd4005d40-dd8`).
   - Al boot:
     - VirtIO-blk PCI modern inizializzato (`KERNEL_VIRTIO_BLK_BDF: 0x10`, `KERNEL_VIRTIO_BLK_SECTORS: 0x80`, `KERNEL_VIRTIO_BLK_READY`), con verifica di lettura/scrittura/flush su settore di scratch e ripristino.
     - AHCI SATA inizializzato su ICH9 (`KERNEL_AHCI_BDF: 0xfa`, `KERNEL_AHCI_PORT: 0x0`, `KERNEL_AHCI_SECTORS: 0xfc000`, `KERNEL_AHCI_READY`), con `IDENTIFY DEVICE` DMA e lettura/scrittura/flush verificati.
     - VFS montato con 2 filesystem (`KERNEL_VFS_MOUNTS: 0x2`): `/` su VirtIO-blk FAT32 (`/HELLO.TXT`, `KERNEL_VFS_ROOT_BYTES: 0x15` = 21 byte) e `/boot` su AHCI SATA ESP FAT16 (`/boot/EFI/BOOT/boot-check.txt`, `KERNEL_VFS_BOOT_BYTES: 0xf` = 15 byte via LFN), più ciclo completo write/sync/cache-invalidate/readback/unlink di `/VERIFY.TXT` (`KERNEL_VFS_READY`).
   - In Ring 3 (`proc`):
     - `worker.elf` apre `/HELLO.TXT` via `SYS_OPEN (6)`, legge 21 byte via `SYS_READ (7)`, verifica il contenuto e chiude l'handle via `SYS_CLOSE (9)` prima di terminare con codice 0.
     - Alla terminazione o `reap` di ogni processo, `x86_process_manager` rilascia gli eventuali file rimasti aperti tramite `kernel_vfs_close_owned`.
   - Nella shell interattiva PS/2:
     - `ls` elenca la root (`SHELL_LS_COUNT: 0x1`, `SHELL_LS_OK`).
     - `write note voidos` crea `/NOTE` su FAT32 (`SHELL_WRITE_BYTES: 0x6`, `SHELL_WRITE_OK`).
     - `cat note` rilegge `/NOTE` (`SHELL_CAT: voidos`, `SHELL_CAT_BYTES: 0x6`, `SHELL_CAT_OK`).
     - `rm note` rimuove `/NOTE` e libera i cluster (`SHELL_RM_OK`).
     - `sync` esegue il flush dei mount e dei device sottostanti (`SHELL_SYNC_OK`).
   - Stabilità e memoria: `FreePages: 11834` costanti in tutti i 10 boot, `Overruns: 0`, 24 job AP completati per boot, preemption attiva su tutte le 4 CPU.
2. **`--ordinary-shell --smp 8 --boots 3`:** **3/3 PASS** (`kernel/build/qemu-runner-18ea76b2af5387e4-26fc`), `FreePages: 11606` costanti, `Overruns: 0`, `SharedIdtCpus: 8`, `VisitedCpus: 255`.
3. **`--ordinary-no-ps2 --smp 4 --boots 1` (`464dd6b`):** **1/1 PASS** (`kernel/build/qemu-runner-18ea76c6b9754000-6284`), VirtIO-blk, AHCI e VFS attivi e verificati anche senza controller PS/2, `FreePages: 11837`.

### Campagna aggiornata dopo fix robustezza FAT e `IF=1` su VFS/cache (`kernel.elf` SHA-256 `679BBED6E6AD0DC14D18E3979982ED5B9F7F39F25829E43C730FFCD33BBC8D19`)

- **Fix verificati:**
  - `truncate` e `unlink` in [write.c](../../kernel/vfs/write.c) aggiornano la directory entry su disco **prima** di liberare la catena dei cluster nella FAT; verificato in `vfs_fat_host` (ASan, PASS) tramite iniezione di `KERNEL_STORAGE_ERR_IO` sul settore directory (`LBA 8`), controllando che i cluster FAT restino allocati e i dati integri.
  - [vfs.c](../../kernel/vfs/vfs.c) e [cache.c](../../kernel/storage/cache.c) usano un flag `IoBusy` protetto da spinlock breve, mantenendo `IF=1` durante gli attraversamenti FAT e l'I/O sincrono su disco; verificato in `vfs_fat_host` (`ks_irq_state() != 0` in ogni lettura/scrittura del device) e `block_cache_host` (ASan, PASS).
- **Boot QEMU aggiornati:**
  - `--ordinary-shell --smp 4 --boots 10`: **10/10 PASS** (`kernel/build/qemu-runner-18ea7a86f13df900-8b9c`), inclusi `/bin/init.elf`, `/bin/bridge.elf` e `/bin/worker.elf` caricati dal VFS VirtIO-blk da 2048 settori (`KERNEL_VFS_EXEC_OK`).
  - `--ordinary-no-ps2 --smp 4 --boots 1`: **1/1 PASS** (`kernel/build/qemu-runner-18ea7abc6c690edc-7d60`).

## Limiti noti e cosa non è stato provato

- **Hardware fisico:** non provato su PC fisico (provato su QEMU 11.1.0 q35/ICH9 + VirtIO 1.2 PCI modern).
- **Sottodirectory in scrittura e LFN in creazione:** la lettura risolve percorsi annidati (es. `/boot/EFI/BOOT/boot-check.txt`) e nomi LFN; la creazione/scrittura (`kernel_vfs_write_file`) e `unlink` operano su file con nome 8.3 nella directory radice del mount.
- **Polling sincrono nei driver di storage:** VirtIO-blk e AHCI usano DMA fisico con polling limitato da deadline sul clock del kernel sotto spinlock del device; non usano ancora code asincrone guidate da MSI/MSI-X.
