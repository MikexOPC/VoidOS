#ifndef OS_KERNEL_DRIVERS_STORAGE_AHCI_H
#define OS_KERNEL_DRIVERS_STORAGE_AHCI_H

#include <stddef.h>
#include <stdint.h>
#include "kernel/storage/block.h"

// ====== ahci_sata_contract ======

// Intel Serial ATA AHCI 1.3.1 §2.1, §3.1, §3.3, §4.2 & ATA8-ACS §7.14, §7.16, §7.25, §7.63.
#define KAHCI_PCI_ABSENT            0U
#define KAHCI_PCI_READY             1U
#define KAHCI_PCI_REJECTED          2U
#define KAHCI_PCI_UNSUPPORTED       3U

#define AHCI_GHC_HR                 (1U << 0)
#define AHCI_GHC_IE                 (1U << 1)
#define AHCI_GHC_AE                 (1U << 31)

#define AHCI_PXCMD_ST               (1U << 0)
#define AHCI_PXCMD_FRE              (1U << 4)
#define AHCI_PXCMD_FR               (1U << 14)
#define AHCI_PXCMD_CR               (1U << 15)

#define AHCI_PXIS_TFES              (1U << 30)
#define AHCI_PXTFD_ERR              (1U << 0)
#define AHCI_PXTFD_DRQ              (1U << 3)
#define AHCI_PXTFD_BSY              (1U << 7)

#define ATA_CMD_READ_DMA_EXT        0x25U
#define ATA_CMD_WRITE_DMA_EXT       0x35U
#define ATA_CMD_FLUSH_CACHE_EXT     0xEAU
#define ATA_CMD_IDENTIFY            0xECU

#define AHCI_MAX_TRANSFER_SECTORS   8U

typedef struct {
    uint64_t  AbarAddress;
    uint16_t  VendorId;
    uint16_t  DeviceId;
    uint32_t  Revision;
    uint32_t  ClassCode;
    uint32_t  Reserved;
} AHCI_PCI_LAYOUT;

_Static_assert(sizeof(AHCI_PCI_LAYOUT) == 24, "AHCI PCI layout ABI");

// Intel AHCI 1.3.1 §4.2.2: Command Header (32 bytes).
typedef struct {
    uint16_t  Flags;
    uint16_t  PrdtLength;
    volatile uint32_t  PrdByteCount;
    uint32_t  CmdTableBaseLow;
    uint32_t  CmdTableBaseHigh;
    uint32_t  Reserved[4];
} AHCI_CMD_HEADER;

// Intel AHCI 1.3.1 §4.2.3.3: Physical Region Descriptor Table entry (16 bytes).
typedef struct {
    uint32_t  DataBaseLow;
    uint32_t  DataBaseHigh;
    uint32_t  Reserved;
    uint32_t  ByteCountAndInterrupt;
} AHCI_PRDT_ENTRY;

// Intel AHCI 1.3.1 §4.2.3: Command Table (128-byte aligned, 128 + 16 = 144 bytes).
typedef struct {
    uint8_t          CommandFis[64];
    uint8_t          AtapiCmd[16];
    uint8_t          Reserved[48];
    AHCI_PRDT_ENTRY  Prdt[8];
} AHCI_CMD_TABLE;

// Single 4 KiB DMA control page holding 1K-aligned Command List, 256B-aligned FIS, 128B-aligned CmdTable.
typedef struct {
    AHCI_CMD_HEADER  CommandList[32];
    uint8_t          ReceivedFis[256];
    AHCI_CMD_TABLE   CommandTable;
    uint8_t          IdentifyBuffer[512];
} AHCI_DMA_CONTROL;

_Static_assert(sizeof(AHCI_CMD_HEADER) == 32, "AHCI command header size");
_Static_assert(offsetof(AHCI_DMA_CONTROL, CommandList) == 0, "1K-aligned command list");
_Static_assert(offsetof(AHCI_DMA_CONTROL, ReceivedFis) == 1024, "256B-aligned received FIS");
_Static_assert(offsetof(AHCI_DMA_CONTROL, CommandTable) == 1280, "128B-aligned command table");
_Static_assert(sizeof(AHCI_DMA_CONTROL) <= 4096, "AHCI control fits in one page");

typedef int (*AHCI_MMIO_FN)(
    void *context,
    uint32_t offset,
    int write,
    uint32_t *value
);

typedef int (*AHCI_CLOCK_FN)(void *context, uint32_t *ticks_out);
typedef void (*AHCI_BARRIER_FN)(void *context);

typedef struct {
    void             *Context;
    AHCI_MMIO_FN      Mmio;
    AHCI_CLOCK_FN     Clock;
    AHCI_BARRIER_FN   Barrier;
    uint32_t          ClockMask;
    uint32_t          TimeoutTicks;
    uint32_t          MaxPolls;
} AHCI_TRANSPORT;

typedef struct {
    uintptr_t            Self;
    AHCI_TRANSPORT       Transport;
    AHCI_DMA_CONTROL    *Control;
    uint8_t             *DataBuffer;
    uint64_t             ControlPhys;
    uint64_t             DataPhys;
    uint64_t             CapacitySectors;
    uint32_t             PortIndex;
    uint32_t             Version;
    uint32_t             Capabilities;
    uint32_t             Ready;
    KERNEL_BLOCK_DEVICE  BlockDevice;
} KERNEL_AHCI_DEVICE;

uint32_t kahci_pci_probe(
    const uint8_t *config,
    uint32_t bytes,
    uint32_t physical_bits,
    AHCI_PCI_LAYOUT *output
);

uint32_t kahci_port_usable(uint32_t ssts, uint32_t sig);

uint32_t kahci_build_h2d_fis(
    uint8_t command,
    uint64_t lba,
    uint16_t sector_count,
    uint8_t *fis_out
);

uint64_t kahci_identify_sectors(const uint8_t *identify_512, uint32_t bytes);

int kernel_ahci_init(
    KERNEL_AHCI_DEVICE *driver,
    const AHCI_TRANSPORT *transport,
    void *control_page,
    uint64_t control_phys,
    void *data_page,
    uint64_t data_phys
);

int kernel_ahci_command(
    KERNEL_AHCI_DEVICE *driver,
    uint8_t ata_cmd,
    uint64_t lba,
    uint32_t sector_count,
    void *buffer
);

#endif
