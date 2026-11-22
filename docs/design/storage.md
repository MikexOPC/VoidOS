# Design di Storage, Cache, VFS e FAT (Obiettivo 3)

## 1. Obiettivo e confine architetturale

L'immagine unica `ordinary` rileva i dischi a blocchi via PCI (VirtIO-blk e
AHCI SATA), mantiene una cache LRU write-through su `kernel_heap()`, monta un
filesystem FAT (FAT32 e FAT16 ESP) tramite VFS e consente a shell e processi
in Ring 3 di elencare, leggere, creare, modificare, eliminare e sincronizzare
file reali su disco.

`kernel/storage/`, `kernel/vfs/` e `kernel/drivers/storage/` non includono
header di `arch/x86/`. I backend x86 in `arch/x86/kernel/drivers/storage/`
usano `X86_ECAM`, mapping MMIO UC- e `X86_DMA_ARENA`, registrando un
`KERNEL_BLOCK_DEVICE` nel layer comune.

## 2. Ordine dei lock

Ordine strettamente crescente dei rank:

`VFS (120) -> BLOCK_CACHE (130) -> BLOCK_DEVICE (140) -> PROCESS_TABLE (150) -> HEAP (200) -> PHYSICAL (300) -> SCHEDULER (900)`

Nessun lock di storage viene acquisito da contesto IRQ o NMI.

## 3. Strutture e ownership

- **`KERNEL_BLOCK_DEVICE`** ([block.h](../../kernel/storage/block.h)):
  descrive un disco a blocchi (`SectorBytes` 512 o 4096, `TotalSectors`,
  `MaxTransferSectors`, `ReadOnly`, vtable `Read`, `Write`, `Flush`).
- **`KERNEL_BLOCK_CACHE`** ([cache.h](../../kernel/storage/cache.h)):
  cache a linee da 4096 byte (`KERNEL_CACHE_BLOCK_BYTES`) allocata
  dinamicamente su `kernel_heap()` (capacità configurabile fino a 1024 linee,
  default 256 linee = 1 MiB). Politica **LRU in lettura** e **write-through
  in scrittura**: ogni scrittura attraversa subito il `KERNEL_BLOCK_DEVICE`;
  se il device restituisce errore, le linee coinvolte vengono invalidate per
  impedire letture stantie. `flush` propaga la barriera di persistenza al
  device.
- **Driver a blocchi (due backend dietro `KERNEL_BLOCK_DEVICE`)**:
  1. **VirtIO-blk PCI modern** (OASIS VirtIO 1.2 §2, §4.1, §5.2): discovery
     capabilities PCI (`COMMON_CFG`, `NOTIFY_CFG`, `ISR_CFG`, `DEVICE_CFG`),
     negoziazione `VIRTIO_F_VERSION_1`, split virtqueue in DMA, richieste
     `VIRTIO_BLK_T_IN`, `VIRTIO_BLK_T_OUT`, `VIRTIO_BLK_T_FLUSH` con timeout
     su `KERNEL_CLOCK`.
  2. **AHCI SATA** (Intel Serial ATA AHCI 1.3.1 §3, §5 + ATA8-ACS): discovery
     PCI class `01:06:01`, mapping UC- di ABAR (BAR5), HBA `AE`, command list
     e FIS ricevuti in DMA, `IDENTIFY DEVICE` (`0xEC`), `READ DMA EXT`
     (`0x25`), `WRITE DMA EXT` (`0x35`), `FLUSH CACHE EXT` (`0xEA`) con
     timeout su `KERNEL_CLOCK`.
- **VFS e FAT (`kernel/vfs/`)**:
  - Parser Rust `no_std` per MBR/GPT, BPB FAT16/FAT32 (Microsoft EFI FAT32
    Specification / UEFI Spec §13.3), catene di cluster con guardia anti-ciclo,
    directory 8.3 e LFN, normalizzazione dei path.
  - Operazioni VFS in lettura e scrittura: `mount`, `lookup`, `stat`,
    `readdir`, `read`, `write`, `create`, `truncate`, `unlink`, `sync`.
  - Tabella dinamica dei file aperti su `kernel_heap()` ed esposizione a
    Ring 3 (`SYS_OPEN`, `SYS_READ`, `SYS_WRITE_FILE`, `SYS_CLOSE`,
    `SYS_UNLINK`) e alla shell (`ls`, `cat`, `write`, `rm`, `sync`).

## 4. Errori e limiti noti

- Codici espliciti: `KERNEL_STORAGE_OK (0)`, `ERR_ARG (-1)`, `ERR_RANGE (-2)`,
  `ERR_READ_ONLY (-3)`, `ERR_IO (-4)`, `ERR_TIMEOUT (-5)`, `ERR_NOMEM (-6)`,
  `ERR_CORRUPT (-7)`, `ERR_NOT_FOUND (-8)`, `ERR_EXISTS (-9)`,
  `ERR_NOSPC (-10)`.
- Ogni contenuto letto dal disco è trattato come input esterno ostile: bounds,
  allineamento e cicli nella FAT vengono verificati prima dell'uso.
- Limiti dell'Obiettivo 3: un volume montato su `/` (con secondo device
  interrogabile e testabile), I/O sincrono serializzato dal lock del device/
  cache con timeout hardware, nessun journaling su FAT.
