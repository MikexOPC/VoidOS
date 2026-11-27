#ifndef OS_KERNEL_DRIVERS_NET_VIRTIO_NET_H
#define OS_KERNEL_DRIVERS_NET_VIRTIO_NET_H

#include <stddef.h>
#include <stdint.h>
#include "kernel/drivers/net/net_device.h"

// ====== virtio_net_contract ======

// OASIS VirtIO 1.2 §2.1, §2.7, §4.1.4, §5.1: modern PCI network device protocol.
#define KVNET_PCI_ABSENT            0U
#define KVNET_PCI_READY             1U
#define KVNET_PCI_REJECTED          2U
#define KVNET_PCI_UNSUPPORTED       3U

#define VIRTIO_NET_STATUS_ACK       0x01U
#define VIRTIO_NET_STATUS_DRIVER    0x02U
#define VIRTIO_NET_STATUS_DRIVER_OK 0x04U
#define VIRTIO_NET_STATUS_FEAT_OK   0x08U
#define VIRTIO_NET_STATUS_FAILED    0x80U

// OASIS VirtIO 1.2 §5.1.3: Feature bits.
#define VIRTIO_NET_F_MAC            (1U << 5)
#define VIRTIO_NET_F_STATUS         (1U << 16)
#define VIRTIO_NET_F_VERSION_1_HI   (1U << 0)

// OASIS VirtIO 1.2 §5.1.4: Device configuration status bit.
#define VIRTIO_NET_S_LINK_UP        1U

// OASIS VirtIO 1.2 §2.7.5: Split virtqueue descriptor flags.
#define VIRTQ_NET_DESC_F_NEXT       1U
#define VIRTQ_NET_DESC_F_WRITE      2U

#define VIRTIO_NET_REGION_COMMON    0U
#define VIRTIO_NET_REGION_NOTIFY    1U
#define VIRTIO_NET_REGION_ISR       2U
#define VIRTIO_NET_REGION_DEVICE    3U

#define VIRTIO_NET_QUEUE_RX         0U
#define VIRTIO_NET_QUEUE_TX         1U
#define VIRTIO_NET_QUEUE_SIZE       16U
#define VIRTIO_NET_SLOT_BYTES       2048U
#define VIRTIO_NET_DMA_PAGES        18U

typedef struct {
    uint64_t  Address;
    uint64_t  Length;
    uint32_t  Bar;
    uint32_t  Offset;
    uint32_t  Multiplier;
    uint32_t  Reserved;
} VIRTIO_NET_PCI_REGION;

typedef struct {
    VIRTIO_NET_PCI_REGION  Regions[4];
    uint32_t               Capabilities;
    uint32_t               Revision;
    uint32_t               ClassCode;
    uint32_t               DeviceId;
} VIRTIO_NET_PCI_LAYOUT;

// OASIS VirtIO 1.2 §5.1.6: struct virtio_net_hdr_v1 (12 bytes when VIRTIO_F_VERSION_1 negotiated).
typedef struct {
    uint8_t   Flags;
    uint8_t   GsoType;
    uint16_t  HdrLen;
    uint16_t  GsoSize;
    uint16_t  CsumStart;
    uint16_t  CsumOffset;
    uint16_t  NumBuffers;
} VIRTIO_NET_HDR_V1;

_Static_assert(sizeof(VIRTIO_NET_HDR_V1) == 12, "VirtIO 1.2 virtio_net_hdr_v1 must be 12 bytes");

typedef struct {
    uint64_t  Address;
    uint32_t  Length;
    uint16_t  Flags;
    uint16_t  Next;
} VIRTQ_NET_DESC;

typedef struct {
    uint32_t  Id;
    uint32_t  Length;
} VIRTQ_NET_USED_ELEM;

typedef struct {
    VIRTQ_NET_DESC       Descriptors[VIRTIO_NET_QUEUE_SIZE];
    uint16_t             AvailFlags;
    uint16_t             AvailIdx;
    uint16_t             AvailRing[VIRTIO_NET_QUEUE_SIZE];
    uint16_t             UsedEvent;
    uint16_t             Padding[13];
    uint16_t             UsedFlags;
    uint16_t             UsedIdx;
    VIRTQ_NET_USED_ELEM  UsedRing[VIRTIO_NET_QUEUE_SIZE];
    uint16_t             AvailEvent;
    uint16_t             TailPadding[11];
} VIRTIO_NET_DMA_QUEUE;

_Static_assert(offsetof(VIRTIO_NET_DMA_QUEUE, AvailFlags) == 256, "RX/TX avail offset");
_Static_assert(offsetof(VIRTIO_NET_DMA_QUEUE, UsedFlags) == 320, "RX/TX used offset");
_Static_assert(sizeof(VIRTIO_NET_DMA_QUEUE) <= 4096, "VirtIO-net queue control fits in one page");

typedef int (*VIRTIO_NET_MMIO_FN)(
    void *context,
    uint32_t region,
    uint32_t offset,
    uint32_t width,
    int write,
    uint32_t *value
);

typedef int (*VIRTIO_NET_CLOCK_FN)(void *context, uint32_t *ticks_out);
typedef void (*VIRTIO_NET_BARRIER_FN)(void *context);
typedef void (*VIRTIO_NET_WAIT_FN)(void *context, uint64_t expected_seq);
typedef uint64_t (*VIRTIO_NET_IRQ_SEQ_FN)(void *context);

typedef struct {
    void                   *Context;
    VIRTIO_NET_MMIO_FN      Mmio;
    VIRTIO_NET_CLOCK_FN     Clock;
    VIRTIO_NET_BARRIER_FN   Barrier;
    uint32_t                ClockMask;
    uint32_t                TimeoutTicks;
    uint32_t                MaxPolls;
    VIRTIO_NET_WAIT_FN      WaitIrq;
    VIRTIO_NET_IRQ_SEQ_FN   IrqSeq;
    uint32_t                IrqEnabled;
} VIRTIO_NET_TRANSPORT;

typedef struct {
    VIRTIO_NET_TRANSPORT   Transport;
    VIRTIO_NET_DMA_QUEUE  *RxQueue;
    uint64_t               RxQueuePhys;
    VIRTIO_NET_DMA_QUEUE  *TxQueue;
    uint64_t               TxQueuePhys;
    uint8_t               *RxBuffers;
    uint64_t               RxBuffersPhys;
    uint8_t               *TxBuffers;
    uint64_t               TxBuffersPhys;
    uint16_t               RxNotifyOff;
    uint16_t               TxNotifyOff;
    uint16_t               RxAvailIdx;
    uint16_t               RxUsedIdx;
    uint16_t               TxAvailIdx;
    uint16_t               TxUsedIdx;
    uint32_t               TxInFlight;
    uint32_t               TxNextSlot;
    uint32_t               FeaturesLow;
    uint32_t               Ready;
    KERNEL_NET_DEVICE      NetDevice;
} VIRTIO_NET_DEVICE;

uint32_t kernel_virtio_net_pci_probe(
    const uint8_t *config,
    size_t config_bytes,
    uint32_t physical_bits,
    VIRTIO_NET_PCI_LAYOUT *layout_out
);

int kernel_virtio_net_init(
    VIRTIO_NET_DEVICE *dev,
    const VIRTIO_NET_TRANSPORT *transport,
    uint8_t *dma_base,
    uint64_t dma_phys
);

int kernel_virtio_net_send(VIRTIO_NET_DEVICE *dev, const uint8_t *frame, uint32_t length);
int kernel_virtio_net_poll_rx(
    VIRTIO_NET_DEVICE *dev,
    uint8_t *frame_out,
    uint32_t capacity,
    uint32_t *length_out
);
int kernel_virtio_net_link_up(VIRTIO_NET_DEVICE *dev);

#endif
