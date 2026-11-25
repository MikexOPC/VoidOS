#ifndef OS_KERNEL_DRIVERS_STORAGE_VIRTIO_BLK_H
#define OS_KERNEL_DRIVERS_STORAGE_VIRTIO_BLK_H

#include <stddef.h>
#include <stdint.h>
#include "kernel/storage/block.h"

// ====== virtio_blk_contract ======

// OASIS VirtIO 1.2 §2.1, §2.7, §4.1.4, §5.2: modern PCI block device protocol.
#define KVBLK_PCI_ABSENT            0U
#define KVBLK_PCI_READY             1U
#define KVBLK_PCI_REJECTED          2U
#define KVBLK_PCI_UNSUPPORTED       3U

#define VIRTIO_STATUS_ACKNOWLEDGE   0x01U
#define VIRTIO_STATUS_DRIVER        0x02U
#define VIRTIO_STATUS_DRIVER_OK     0x04U
#define VIRTIO_STATUS_FEATURES_OK   0x08U
#define VIRTIO_STATUS_FAILED        0x80U

#define VIRTIO_BLK_F_RO             (1U << 5)
#define VIRTIO_BLK_F_BLK_SIZE       (1U << 6)
#define VIRTIO_BLK_F_FLUSH          (1U << 9)
#define VIRTIO_F_VERSION_1_HIGH     (1U << 0)

#define VIRTIO_BLK_T_IN             0U
#define VIRTIO_BLK_T_OUT            1U
#define VIRTIO_BLK_T_FLUSH          4U

#define VIRTIO_BLK_S_OK             0U
#define VIRTIO_BLK_S_IOERR          1U
#define VIRTIO_BLK_S_UNSUPP         2U

#define VIRTQ_DESC_F_NEXT           1U
#define VIRTQ_DESC_F_WRITE          2U
#define VIRTQ_AVAIL_F_NO_INTERRUPT  1U

#define VIRTIO_BLK_QUEUE_SIZE       8U
#define VIRTIO_BLK_MAX_SECTORS      8U

#define VIRTIO_REGION_COMMON        0U
#define VIRTIO_REGION_NOTIFY        1U
#define VIRTIO_REGION_ISR           2U
#define VIRTIO_REGION_DEVICE        3U

typedef struct {
    uint64_t  Address;
    uint64_t  Length;
    uint32_t  Bar;
    uint32_t  Offset;
    uint32_t  Multiplier;
    uint32_t  Reserved;
} VIRTIO_BLK_PCI_REGION;

typedef struct {
    VIRTIO_BLK_PCI_REGION  Regions[4];
    uint32_t               Capabilities;
    uint32_t               Revision;
    uint32_t               ClassCode;
    uint32_t               DeviceId;
} VIRTIO_BLK_PCI_LAYOUT;

_Static_assert(sizeof(VIRTIO_BLK_PCI_REGION) == 32, "VirtIO-blk PCI region ABI");
_Static_assert(sizeof(VIRTIO_BLK_PCI_LAYOUT) == 144, "VirtIO-blk PCI layout ABI");

typedef struct {
    uint64_t  Address;
    uint32_t  Length;
    uint16_t  Flags;
    uint16_t  Next;
} VIRTQ_BLK_DESC;

typedef struct {
    uint32_t  Id;
    uint32_t  Length;
} VIRTQ_BLK_USED_ELEM;

typedef struct {
    uint32_t  Type;
    uint32_t  Reserved;
    uint64_t  Sector;
} VIRTIO_BLK_REQ_HEADER;

typedef struct {
    VIRTQ_BLK_DESC         Descriptors[VIRTIO_BLK_QUEUE_SIZE];
    uint16_t               AvailFlags;
    uint16_t               AvailIdx;
    uint16_t               AvailRing[VIRTIO_BLK_QUEUE_SIZE];
    uint16_t               UsedEvent;
    uint16_t               Padding[5];
    uint16_t               UsedFlags;
    uint16_t               UsedIdx;
    VIRTQ_BLK_USED_ELEM    UsedRing[VIRTIO_BLK_QUEUE_SIZE];
    uint16_t               AvailEvent;
    uint16_t               TailPadding[11];
    VIRTIO_BLK_REQ_HEADER  Header;
    volatile uint8_t       Status;
    uint8_t                HeaderPadding[15];
} VIRTIO_BLK_DMA_CONTROL;

_Static_assert(offsetof(VIRTIO_BLK_DMA_CONTROL, AvailFlags) == 128, "Virtqueue avail offset");
_Static_assert(offsetof(VIRTIO_BLK_DMA_CONTROL, UsedFlags) == 160, "Virtqueue used offset");
_Static_assert(offsetof(VIRTIO_BLK_DMA_CONTROL, Header) == 256, "Request header offset");
_Static_assert(sizeof(VIRTIO_BLK_DMA_CONTROL) <= 4096, "Control fits in one page");

typedef int (*VIRTIO_BLK_MMIO_FN)(
    void *context,
    uint32_t region,
    uint32_t offset,
    uint32_t width,
    int write,
    uint32_t *value
);

typedef int (*VIRTIO_BLK_CLOCK_FN)(void *context, uint32_t *ticks_out);
typedef void (*VIRTIO_BLK_BARRIER_FN)(void *context);
typedef void (*VIRTIO_BLK_WAIT_FN)(void *context, uint64_t expected_seq);
typedef uint64_t (*VIRTIO_BLK_IRQ_SEQ_FN)(void *context);

typedef struct {
    void                   *Context;
    VIRTIO_BLK_MMIO_FN      Mmio;
    VIRTIO_BLK_CLOCK_FN     Clock;
    VIRTIO_BLK_BARRIER_FN   Barrier;
    uint32_t                ClockMask;
    uint32_t                TimeoutTicks;
    uint32_t                MaxPolls;
    VIRTIO_BLK_WAIT_FN      Wait;
    VIRTIO_BLK_IRQ_SEQ_FN   IrqSeq;
    uint32_t                IrqEnabled;
} VIRTIO_BLK_TRANSPORT;

typedef struct {
    uintptr_t                Self;
    VIRTIO_BLK_TRANSPORT     Transport;
    VIRTIO_BLK_DMA_CONTROL  *Control;
    uint8_t                 *DataBuffer;
    uint64_t                 ControlPhys;
    uint64_t                 DataPhys;
    uint64_t                 CapacitySectors;
    uint32_t                 SectorBytes;
    uint32_t                 FeaturesLow;
    uint32_t                 FeaturesHigh;
    uint16_t                 QueueSize;
    uint16_t                 NotifyOff;
    uint16_t                 AvailIdx;
    uint16_t                 LastUsedIdx;
    uint32_t                 ReadOnly;
    uint32_t                 FlushSupported;
    uint32_t                 Ready;
    KERNEL_BLOCK_DEVICE      BlockDevice;
} KERNEL_VIRTIO_BLK;

uint32_t kvblk_pci_probe(
    const uint8_t *config,
    uint32_t bytes,
    uint32_t physical_bits,
    VIRTIO_BLK_PCI_LAYOUT *output
);

uint32_t kvblk_features_select(
    uint32_t device_low,
    uint32_t device_high,
    uint32_t *driver_low_out,
    uint32_t *driver_high_out
);

uint32_t kvblk_queue_size_choose(uint32_t maximum);
uint32_t kvblk_config_validate(uint64_t capacity_sectors, uint32_t blk_size, uint32_t has_blk_size);

int kernel_virtio_blk_init(
    KERNEL_VIRTIO_BLK *driver,
    const VIRTIO_BLK_TRANSPORT *transport,
    void *control_page,
    uint64_t control_phys,
    void *data_page,
    uint64_t data_phys
);

int kernel_virtio_blk_request(
    KERNEL_VIRTIO_BLK *driver,
    uint32_t type,
    uint64_t lba,
    uint32_t sector_count,
    void *buffer
);

#endif
