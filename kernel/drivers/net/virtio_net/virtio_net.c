#include "virtio_net.h"

// OASIS VirtIO 1.2 §4.1.4.3: virtio_pci_common_cfg register offsets.
#define COMMON_DFSELECT         0x00U
#define COMMON_DF               0x04U
#define COMMON_GFSELECT         0x08U
#define COMMON_GF               0x0CU
#define COMMON_MSIX_CONFIG      0x10U
#define COMMON_NUMQ             0x12U
#define COMMON_STATUS           0x14U
#define COMMON_CFGGEN           0x15U
#define COMMON_Q_SELECT         0x16U
#define COMMON_Q_SIZE           0x18U
#define COMMON_Q_MSIX_VECTOR    0x1AU
#define COMMON_Q_ENABLE         0x1CU
#define COMMON_Q_NOTIFY_OFF     0x1EU
#define COMMON_Q_DESCLO         0x20U
#define COMMON_Q_DESCHI         0x24U
#define COMMON_Q_AVAILLO        0x28U
#define COMMON_Q_AVAILHI        0x2CU
#define COMMON_Q_USEDLO         0x30U
#define COMMON_Q_USEDHI         0x34U

// OASIS VirtIO 1.2 §5.1.4: virtio_net_config register offsets (region 3).
#define NET_CFG_MAC_BASE        0x00U
#define NET_CFG_STATUS          0x06U

// ====== read_le16 ======

static uint16_t read_le16(const uint8_t *bytes, size_t offset)
{
    return (uint16_t)bytes[offset] | ((uint16_t)bytes[offset + 1U] << 8);
}

// ====== read_le32 ======

static uint32_t read_le32(const uint8_t *bytes, size_t offset)
{
    return (uint32_t)bytes[offset]
        | ((uint32_t)bytes[offset + 1U] << 8)
        | ((uint32_t)bytes[offset + 2U] << 16)
        | ((uint32_t)bytes[offset + 3U] << 24);
}

// ====== zero_bytes ======

static void zero_bytes(void *ptr, size_t bytes)
{
    uint8_t *dst = (uint8_t *)ptr;
    for (size_t i = 0; i < bytes; ++i)
        dst[i] = 0U;
}

// ====== copy_bytes ======

static void copy_bytes(void *dst_ptr, const void *src_ptr, size_t bytes)
{
    uint8_t *dst = (uint8_t *)dst_ptr;
    const uint8_t *src = (const uint8_t *)src_ptr;
    for (size_t i = 0; i < bytes; ++i)
        dst[i] = src[i];
}

// ====== mmio_read_private ======

static int mmio_read_private(
    const VIRTIO_NET_TRANSPORT *transport,
    uint32_t region,
    uint32_t offset,
    uint32_t width,
    uint32_t *value
)
{
    *value = 0U;
    return transport->Mmio(transport->Context, region, offset, width, 0, value);
}

// ====== mmio_write_private ======

static int mmio_write_private(
    const VIRTIO_NET_TRANSPORT *transport,
    uint32_t region,
    uint32_t offset,
    uint32_t width,
    uint32_t value
)
{
    uint32_t temp = value;
    return transport->Mmio(transport->Context, region, offset, width, 1, &temp);
}

// ====== kernel_virtio_net_pci_probe ======

// OASIS VirtIO 1.2 §4.1.2 & §4.1.4: PCI Vendor 0x1AF4, Device 0x1041 (modern) or 0x1000 (transitional
// with Subsystem Device ID 1); requires PCI capabilities 1 (COMMON), 2 (NOTIFY), 3 (ISR), 4 (DEVICE).
uint32_t kernel_virtio_net_pci_probe(
    const uint8_t *config,
    size_t config_bytes,
    uint32_t physical_bits,
    VIRTIO_NET_PCI_LAYOUT *layout_out
)
{
    if (!config || !layout_out || config_bytes < 256U || physical_bits < 32U || physical_bits > 52U)
        return KVNET_PCI_REJECTED;

    zero_bytes(layout_out, sizeof(*layout_out));

    uint16_t vendor = read_le16(config, 0x00U);
    uint16_t device = read_le16(config, 0x02U);
    if (vendor != 0x1AF4U)
        return KVNET_PCI_ABSENT;
    if (device != 0x1041U && !(device == 0x1000U && read_le16(config, 0x2EU) == 1U))
        return KVNET_PCI_ABSENT;

    uint8_t header_type = config[0x0EU] & 0x7FU;
    if (header_type != 0U)
        return KVNET_PCI_UNSUPPORTED;

    if ((config[0x06U] & 0x10U) == 0U)
        return KVNET_PCI_UNSUPPORTED;

    uint64_t phys_limit = (physical_bits == 64U) ? UINT64_MAX : (1ULL << physical_bits);
    uint64_t bar_addr[6] = {0};
    for (uint32_t b = 0; b < 6U; ++b) {
        uint32_t lo = read_le32(config, 0x10U + (size_t)b * 4U);
        if ((lo & 1U) != 0U)
            continue;
        uint64_t addr = (uint64_t)(lo & ~0xFU);
        if ((lo & 0x6U) == 0x4U && b + 1U < 6U) {
            uint32_t hi = read_le32(config, 0x10U + (size_t)(b + 1U) * 4U);
            addr |= ((uint64_t)hi << 32);
            bar_addr[b] = addr;
            ++b;
        } else {
            bar_addr[b] = addr;
        }
    }

    uint32_t seen_mask = 0U;
    uint32_t cap = config[0x34U] & 0xFCU;
    for (uint32_t steps = 0; cap >= 0x40U && cap + 16U <= config_bytes && steps < 48U; ++steps) {
        uint8_t cap_id = config[cap];
        uint8_t next = config[cap + 1U] & 0xFCU;
        uint8_t cap_len = config[cap + 2U];
        if (cap_id == 0x09U && cap_len >= 16U && cap + cap_len <= config_bytes) {
            uint8_t cfg_type = config[cap + 3U];
            uint8_t bar = config[cap + 4U];
            if (cfg_type >= 1U && cfg_type <= 4U && bar < 6U && bar_addr[bar] != 0ULL) {
                uint32_t off = read_le32(config, cap + 8U);
                uint32_t len = read_le32(config, cap + 12U);
                uint32_t mult = (cfg_type == 2U && cap_len >= 20U) ? read_le32(config, cap + 16U) : 0U;
                uint64_t full_addr = bar_addr[bar] + (uint64_t)off;
                if (len > 0U && full_addr >= bar_addr[bar] && full_addr < phys_limit &&
                    (uint64_t)len <= phys_limit - full_addr) {
                    uint32_t idx = (uint32_t)cfg_type - 1U;
                    if ((seen_mask & (1U << idx)) == 0U) {
                        layout_out->Regions[idx].Address = full_addr;
                        layout_out->Regions[idx].Length = len;
                        layout_out->Regions[idx].Bar = bar;
                        layout_out->Regions[idx].Offset = off;
                        layout_out->Regions[idx].Multiplier = mult;
                        seen_mask |= (1U << idx);
                    }
                }
            }
        }
        if (!next || next == cap)
            break;
        cap = next;
    }

    if (seen_mask != 0x0FU)
        return KVNET_PCI_UNSUPPORTED;

    layout_out->Capabilities = seen_mask;
    layout_out->Revision = config[0x08U];
    layout_out->ClassCode = ((uint32_t)config[0x0BU] << 16)
        | ((uint32_t)config[0x0AU] << 8)
        | config[0x09U];
    layout_out->DeviceId = device;
    return KVNET_PCI_READY;
}

// ====== wait_reset_private ======

static int wait_reset_private(const VIRTIO_NET_TRANSPORT *transport)
{
    if (!mmio_write_private(transport, VIRTIO_NET_REGION_COMMON, COMMON_STATUS, 1U, 0U))
        return KERNEL_NET_ERR_IO;

    uint32_t start = 0U;
    if (!transport->Clock(transport->Context, &start))
        return KERNEL_NET_ERR_IO;

    uint32_t elapsed = 0U;
    uint32_t last = start;
    for (uint32_t poll = 0; poll < transport->MaxPolls; ++poll) {
        uint32_t status = 0xFFU;
        if (!mmio_read_private(transport, VIRTIO_NET_REGION_COMMON, COMMON_STATUS, 1U, &status))
            return KERNEL_NET_ERR_IO;
        if ((status & 0xFFU) == 0U)
            return KERNEL_NET_OK;

        uint32_t now = 0U;
        if (!transport->Clock(transport->Context, &now))
            return KERNEL_NET_ERR_IO;
        elapsed += (now - last) & transport->ClockMask;
        last = now;
        if (elapsed >= transport->TimeoutTicks)
            return KERNEL_NET_ERR_TIMEOUT;
    }
    return KERNEL_NET_ERR_TIMEOUT;
}

// ====== read_mac_and_status_private ======

// OASIS VirtIO 1.2 §2.5.4 & §5.1.4: read mac[0..5] and status with config_generation stability check.
static int read_mac_and_status_private(
    const VIRTIO_NET_TRANSPORT *transport,
    uint32_t features_low,
    uint8_t mac_out[6],
    uint32_t *link_up_out
)
{
    for (uint32_t attempt = 0; attempt < 16U; ++attempt) {
        uint32_t gen_before = 0U, gen_after = 0U;
        if (!mmio_read_private(transport, VIRTIO_NET_REGION_COMMON, COMMON_CFGGEN, 1U, &gen_before))
            return KERNEL_NET_ERR_IO;

        if ((features_low & VIRTIO_NET_F_MAC) != 0U) {
            for (uint32_t i = 0; i < 6U; ++i) {
                uint32_t b = 0U;
                if (!mmio_read_private(transport, VIRTIO_NET_REGION_DEVICE, NET_CFG_MAC_BASE + i, 1U, &b))
                    return KERNEL_NET_ERR_IO;
                mac_out[i] = (uint8_t)(b & 0xFFU);
            }
        } else {
            mac_out[0] = 0x52U;
            mac_out[1] = 0x54U;
            mac_out[2] = 0x00U;
            mac_out[3] = 0x12U;
            mac_out[4] = 0x34U;
            mac_out[5] = 0x56U;
        }

        uint32_t link_up = 1U;
        if ((features_low & VIRTIO_NET_F_STATUS) != 0U) {
            uint32_t st = 0U;
            if (!mmio_read_private(transport, VIRTIO_NET_REGION_DEVICE, NET_CFG_STATUS, 2U, &st))
                return KERNEL_NET_ERR_IO;
            link_up = (st & VIRTIO_NET_S_LINK_UP) != 0U ? 1U : 0U;
        }

        if (!mmio_read_private(transport, VIRTIO_NET_REGION_COMMON, COMMON_CFGGEN, 1U, &gen_after))
            return KERNEL_NET_ERR_IO;

        if ((gen_before & 0xFFU) == (gen_after & 0xFFU)) {
            *link_up_out = link_up;
            return KERNEL_NET_OK;
        }
    }
    return KERNEL_NET_ERR_TIMEOUT;
}

// ====== setup_queue_private ======

static int setup_queue_private(
    const VIRTIO_NET_TRANSPORT *transport,
    uint16_t queue_index,
    uint64_t queue_phys,
    uint16_t *notify_off_out
)
{
    if (!mmio_write_private(transport, VIRTIO_NET_REGION_COMMON, COMMON_Q_SELECT, 2U, queue_index))
        return KERNEL_NET_ERR_IO;

    uint32_t max_size = 0U;
    if (!mmio_read_private(transport, VIRTIO_NET_REGION_COMMON, COMMON_Q_SIZE, 2U, &max_size))
        return KERNEL_NET_ERR_IO;
    if (max_size < VIRTIO_NET_QUEUE_SIZE)
        return KERNEL_NET_ERR_UNSUPPORTED;

    if (!mmio_write_private(transport, VIRTIO_NET_REGION_COMMON, COMMON_Q_SIZE, 2U, VIRTIO_NET_QUEUE_SIZE))
        return KERNEL_NET_ERR_IO;

    uint32_t msix_vec = transport->IrqEnabled ? 0U : 0xFFFFU;
    if (!mmio_write_private(transport, VIRTIO_NET_REGION_COMMON, COMMON_Q_MSIX_VECTOR, 2U, msix_vec))
        return KERNEL_NET_ERR_IO;

    uint64_t desc_phys = queue_phys;
    uint64_t avail_phys = queue_phys + offsetof(VIRTIO_NET_DMA_QUEUE, AvailFlags);
    uint64_t used_phys = queue_phys + offsetof(VIRTIO_NET_DMA_QUEUE, UsedFlags);

    if (!mmio_write_private(transport, VIRTIO_NET_REGION_COMMON, COMMON_Q_DESCLO, 4U, (uint32_t)desc_phys) ||
        !mmio_write_private(transport, VIRTIO_NET_REGION_COMMON, COMMON_Q_DESCHI, 4U, (uint32_t)(desc_phys >> 32)) ||
        !mmio_write_private(transport, VIRTIO_NET_REGION_COMMON, COMMON_Q_AVAILLO, 4U, (uint32_t)avail_phys) ||
        !mmio_write_private(transport, VIRTIO_NET_REGION_COMMON, COMMON_Q_AVAILHI, 4U, (uint32_t)(avail_phys >> 32)) ||
        !mmio_write_private(transport, VIRTIO_NET_REGION_COMMON, COMMON_Q_USEDLO, 4U, (uint32_t)used_phys) ||
        !mmio_write_private(transport, VIRTIO_NET_REGION_COMMON, COMMON_Q_USEDHI, 4U, (uint32_t)(used_phys >> 32)))
        return KERNEL_NET_ERR_IO;

    uint32_t notify_off = 0U;
    if (!mmio_read_private(transport, VIRTIO_NET_REGION_COMMON, COMMON_Q_NOTIFY_OFF, 2U, &notify_off))
        return KERNEL_NET_ERR_IO;

    if (!mmio_write_private(transport, VIRTIO_NET_REGION_COMMON, COMMON_Q_ENABLE, 2U, 1U))
        return KERNEL_NET_ERR_IO;

    *notify_off_out = (uint16_t)notify_off;
    return KERNEL_NET_OK;
}

// ====== post_rx_slot_private ======

static void post_rx_slot_private(VIRTIO_NET_DEVICE *dev, uint16_t slot)
{
    uint64_t buf_phys = dev->RxBuffersPhys + (uint64_t)slot * VIRTIO_NET_SLOT_BYTES;
    dev->RxQueue->Descriptors[slot].Address = buf_phys;
    dev->RxQueue->Descriptors[slot].Length = VIRTIO_NET_SLOT_BYTES;
    dev->RxQueue->Descriptors[slot].Flags = VIRTQ_NET_DESC_F_WRITE;
    dev->RxQueue->Descriptors[slot].Next = 0U;

    uint16_t ring_idx = (uint16_t)(dev->RxAvailIdx % VIRTIO_NET_QUEUE_SIZE);
    dev->RxQueue->AvailRing[ring_idx] = slot;
    dev->Transport.Barrier(dev->Transport.Context);
    dev->RxAvailIdx = (uint16_t)(dev->RxAvailIdx + 1U);
    dev->RxQueue->AvailIdx = dev->RxAvailIdx;
    dev->Transport.Barrier(dev->Transport.Context);
}

// ====== drain_tx_used_private ======

static void drain_tx_used_private(VIRTIO_NET_DEVICE *dev)
{
    dev->Transport.Barrier(dev->Transport.Context);
    volatile uint16_t *used_idx_ptr = &dev->TxQueue->UsedIdx;
    while (dev->TxUsedIdx != *used_idx_ptr) {
        dev->TxUsedIdx = (uint16_t)(dev->TxUsedIdx + 1U);
        if (dev->TxInFlight > 0U)
            --dev->TxInFlight;
    }
}

// ====== ops_send_private ======

static int ops_send_private(void *context, const uint8_t *frame, uint32_t length)
{
    return kernel_virtio_net_send((VIRTIO_NET_DEVICE *)context, frame, length);
}

// ====== ops_poll_rx_private ======

static int ops_poll_rx_private(void *context, uint8_t *frame_out, uint32_t capacity, uint32_t *length_out)
{
    return kernel_virtio_net_poll_rx((VIRTIO_NET_DEVICE *)context, frame_out, capacity, length_out);
}

// ====== ops_link_up_private ======

static int ops_link_up_private(void *context)
{
    return kernel_virtio_net_link_up((VIRTIO_NET_DEVICE *)context);
}

// ====== ops_wait_private ======

static void ops_wait_private(void *context, uint32_t timeout_ms)
{
    VIRTIO_NET_DEVICE *dev = (VIRTIO_NET_DEVICE *)context;
    if (!dev || !dev->Ready || !dev->Transport.IrqEnabled || !dev->Transport.WaitIrq || !dev->Transport.IrqSeq)
        return;
    (void)timeout_ms;
    uint64_t seq = dev->Transport.IrqSeq(dev->Transport.Context);
    dev->Transport.WaitIrq(dev->Transport.Context, seq);
}

static const KERNEL_NET_OPS virtio_net_ops = {
    ops_send_private,
    ops_poll_rx_private,
    ops_link_up_private,
    ops_wait_private,
};

// ====== kernel_virtio_net_init ======

// OASIS VirtIO 1.2 §3.1.1 & §5.1: reset, ACK, DRIVER, negotiate MAC/STATUS/VERSION_1,
// FEATURES_OK, configure receiveq1 (0) and transmitq1 (1), pre-post RX buffers, DRIVER_OK.
int kernel_virtio_net_init(
    VIRTIO_NET_DEVICE *dev,
    const VIRTIO_NET_TRANSPORT *transport,
    uint8_t *dma_base,
    uint64_t dma_phys
)
{
    if (!dev || !transport || !transport->Mmio || !transport->Clock || !transport->Barrier ||
        !dma_base || !dma_phys || (dma_phys & 4095ULL) != 0ULL)
        return KERNEL_NET_ERR_PARAM;

    zero_bytes(dev, sizeof(*dev));
    zero_bytes(dma_base, (size_t)VIRTIO_NET_DMA_PAGES * 4096U);

    dev->Transport = *transport;
    dev->RxQueue = (VIRTIO_NET_DMA_QUEUE *)(dma_base + 0U);
    dev->RxQueuePhys = dma_phys + 0ULL;
    dev->TxQueue = (VIRTIO_NET_DMA_QUEUE *)(dma_base + 4096U);
    dev->TxQueuePhys = dma_phys + 4096ULL;
    dev->RxBuffers = dma_base + 8192U;
    dev->RxBuffersPhys = dma_phys + 8192ULL;
    dev->TxBuffers = dma_base + 8192U + (size_t)VIRTIO_NET_QUEUE_SIZE * VIRTIO_NET_SLOT_BYTES;
    dev->TxBuffersPhys = dma_phys + 8192ULL + (uint64_t)VIRTIO_NET_QUEUE_SIZE * VIRTIO_NET_SLOT_BYTES;

    int rc = wait_reset_private(transport);
    if (rc != KERNEL_NET_OK)
        return rc;

    uint32_t status = VIRTIO_NET_STATUS_ACK;
    if (!mmio_write_private(transport, VIRTIO_NET_REGION_COMMON, COMMON_STATUS, 1U, status))
        return KERNEL_NET_ERR_IO;
    status |= VIRTIO_NET_STATUS_DRIVER;
    if (!mmio_write_private(transport, VIRTIO_NET_REGION_COMMON, COMMON_STATUS, 1U, status))
        return KERNEL_NET_ERR_IO;

    uint32_t dev_feat_lo = 0U, dev_feat_hi = 0U;
    if (!mmio_write_private(transport, VIRTIO_NET_REGION_COMMON, COMMON_DFSELECT, 4U, 0U) ||
        !mmio_read_private(transport, VIRTIO_NET_REGION_COMMON, COMMON_DF, 4U, &dev_feat_lo) ||
        !mmio_write_private(transport, VIRTIO_NET_REGION_COMMON, COMMON_DFSELECT, 4U, 1U) ||
        !mmio_read_private(transport, VIRTIO_NET_REGION_COMMON, COMMON_DF, 4U, &dev_feat_hi))
        return KERNEL_NET_ERR_IO;

    if ((dev_feat_hi & VIRTIO_NET_F_VERSION_1_HI) == 0U)
        return KERNEL_NET_ERR_UNSUPPORTED;

    uint32_t drv_feat_lo = dev_feat_lo & (VIRTIO_NET_F_MAC | VIRTIO_NET_F_STATUS);
    uint32_t drv_feat_hi = VIRTIO_NET_F_VERSION_1_HI;
    if (!mmio_write_private(transport, VIRTIO_NET_REGION_COMMON, COMMON_GFSELECT, 4U, 0U) ||
        !mmio_write_private(transport, VIRTIO_NET_REGION_COMMON, COMMON_GF, 4U, drv_feat_lo) ||
        !mmio_write_private(transport, VIRTIO_NET_REGION_COMMON, COMMON_GFSELECT, 4U, 1U) ||
        !mmio_write_private(transport, VIRTIO_NET_REGION_COMMON, COMMON_GF, 4U, drv_feat_hi))
        return KERNEL_NET_ERR_IO;

    status |= VIRTIO_NET_STATUS_FEAT_OK;
    if (!mmio_write_private(transport, VIRTIO_NET_REGION_COMMON, COMMON_STATUS, 1U, status))
        return KERNEL_NET_ERR_IO;

    uint32_t readback = 0U;
    if (!mmio_read_private(transport, VIRTIO_NET_REGION_COMMON, COMMON_STATUS, 1U, &readback) ||
        (readback & VIRTIO_NET_STATUS_FEAT_OK) == 0U)
        return KERNEL_NET_ERR_UNSUPPORTED;

    dev->FeaturesLow = drv_feat_lo;

    uint32_t num_queues = 0U;
    if (!mmio_read_private(transport, VIRTIO_NET_REGION_COMMON, COMMON_NUMQ, 2U, &num_queues) ||
        num_queues < 2U)
        return KERNEL_NET_ERR_UNSUPPORTED;

    uint32_t msix_cfg = transport->IrqEnabled ? 0U : 0xFFFFU;
    (void)mmio_write_private(transport, VIRTIO_NET_REGION_COMMON, COMMON_MSIX_CONFIG, 2U, msix_cfg);

    rc = read_mac_and_status_private(transport, drv_feat_lo, dev->NetDevice.Mac, &dev->NetDevice.LinkUp);
    if (rc != KERNEL_NET_OK)
        return rc;

    rc = setup_queue_private(transport, VIRTIO_NET_QUEUE_RX, dev->RxQueuePhys, &dev->RxNotifyOff);
    if (rc != KERNEL_NET_OK)
        return rc;

    rc = setup_queue_private(transport, VIRTIO_NET_QUEUE_TX, dev->TxQueuePhys, &dev->TxNotifyOff);
    if (rc != KERNEL_NET_OK)
        return rc;

    for (uint16_t slot = 0; slot < VIRTIO_NET_QUEUE_SIZE; ++slot)
        post_rx_slot_private(dev, slot);

    status |= VIRTIO_NET_STATUS_DRIVER_OK;
    if (!mmio_write_private(transport, VIRTIO_NET_REGION_COMMON, COMMON_STATUS, 1U, status))
        return KERNEL_NET_ERR_IO;

    if (!mmio_write_private(transport, VIRTIO_NET_REGION_NOTIFY, dev->RxNotifyOff, 2U, VIRTIO_NET_QUEUE_RX))
        return KERNEL_NET_ERR_IO;

    dev->NetDevice.Ops = &virtio_net_ops;
    dev->NetDevice.Context = dev;
    dev->NetDevice.Mtu = KERNEL_NET_MTU;
    dev->Ready = 1U;
    return KERNEL_NET_OK;
}

// ====== kernel_virtio_net_send ======

int kernel_virtio_net_send(VIRTIO_NET_DEVICE *dev, const uint8_t *frame, uint32_t length)
{
    if (!dev || !dev->Ready || !frame || length < KERNEL_NET_ETH_HEADER_BYTES ||
        length > KERNEL_NET_MAX_FRAME_BYTES)
        return KERNEL_NET_ERR_PARAM;

    drain_tx_used_private(dev);
    if (dev->TxInFlight >= VIRTIO_NET_QUEUE_SIZE)
        return KERNEL_NET_ERR_FULL;

    uint16_t slot = (uint16_t)(dev->TxNextSlot % VIRTIO_NET_QUEUE_SIZE);
    dev->TxNextSlot = (dev->TxNextSlot + 1U) % VIRTIO_NET_QUEUE_SIZE;

    uint8_t *buf = dev->TxBuffers + (size_t)slot * VIRTIO_NET_SLOT_BYTES;
    uint64_t buf_phys = dev->TxBuffersPhys + (uint64_t)slot * VIRTIO_NET_SLOT_BYTES;

    // OASIS VirtIO 1.2 §5.1.6: prepend zeroed 12-byte virtio_net_hdr_v1 (no offload flags).
    zero_bytes(buf, sizeof(VIRTIO_NET_HDR_V1));
    copy_bytes(buf + sizeof(VIRTIO_NET_HDR_V1), frame, length);

    uint32_t total_len = (uint32_t)sizeof(VIRTIO_NET_HDR_V1) + length;
    dev->TxQueue->Descriptors[slot].Address = buf_phys;
    dev->TxQueue->Descriptors[slot].Length = total_len;
    dev->TxQueue->Descriptors[slot].Flags = 0U;
    dev->TxQueue->Descriptors[slot].Next = 0U;

    uint16_t ring_idx = (uint16_t)(dev->TxAvailIdx % VIRTIO_NET_QUEUE_SIZE);
    dev->TxQueue->AvailRing[ring_idx] = slot;
    dev->Transport.Barrier(dev->Transport.Context);
    dev->TxAvailIdx = (uint16_t)(dev->TxAvailIdx + 1U);
    dev->TxQueue->AvailIdx = dev->TxAvailIdx;
    ++dev->TxInFlight;
    dev->Transport.Barrier(dev->Transport.Context);

    if (!mmio_write_private(&dev->Transport, VIRTIO_NET_REGION_NOTIFY, dev->TxNotifyOff, 2U, VIRTIO_NET_QUEUE_TX))
        return KERNEL_NET_ERR_IO;

    ++dev->NetDevice.TxPackets;
    dev->NetDevice.TxBytes += length;
    return KERNEL_NET_OK;
}

// ====== kernel_virtio_net_poll_rx ======

int kernel_virtio_net_poll_rx(
    VIRTIO_NET_DEVICE *dev,
    uint8_t *frame_out,
    uint32_t capacity,
    uint32_t *length_out
)
{
    if (!dev || !dev->Ready || !frame_out || !length_out || capacity < KERNEL_NET_ETH_HEADER_BYTES)
        return KERNEL_NET_ERR_PARAM;

    *length_out = 0U;
    dev->Transport.Barrier(dev->Transport.Context);
    volatile uint16_t *used_idx_ptr = &dev->RxQueue->UsedIdx;
    if (dev->RxUsedIdx == *used_idx_ptr)
        return KERNEL_NET_ERR_EMPTY;

    uint16_t ring_idx = (uint16_t)(dev->RxUsedIdx % VIRTIO_NET_QUEUE_SIZE);
    uint32_t slot = dev->RxQueue->UsedRing[ring_idx].Id;
    uint32_t used_len = dev->RxQueue->UsedRing[ring_idx].Length;
    dev->RxUsedIdx = (uint16_t)(dev->RxUsedIdx + 1U);

    if (slot >= VIRTIO_NET_QUEUE_SIZE) {
        ++dev->NetDevice.RxDrops;
        return KERNEL_NET_ERR_IO;
    }

    int rc = KERNEL_NET_OK;
    if (used_len <= sizeof(VIRTIO_NET_HDR_V1) || used_len > VIRTIO_NET_SLOT_BYTES) {
        ++dev->NetDevice.RxDrops;
        rc = KERNEL_NET_ERR_EMPTY;
    } else {
        uint32_t payload_len = used_len - (uint32_t)sizeof(VIRTIO_NET_HDR_V1);
        if (payload_len > capacity) {
            ++dev->NetDevice.RxDrops;
            rc = KERNEL_NET_ERR_EMPTY;
        } else {
            const uint8_t *buf = dev->RxBuffers + (size_t)slot * VIRTIO_NET_SLOT_BYTES;
            copy_bytes(frame_out, buf + sizeof(VIRTIO_NET_HDR_V1), payload_len);
            *length_out = payload_len;
            ++dev->NetDevice.RxPackets;
            dev->NetDevice.RxBytes += payload_len;
        }
    }

    post_rx_slot_private(dev, (uint16_t)slot);
    (void)mmio_write_private(&dev->Transport, VIRTIO_NET_REGION_NOTIFY, dev->RxNotifyOff, 2U, VIRTIO_NET_QUEUE_RX);
    return rc;
}

// ====== kernel_virtio_net_link_up ======

int kernel_virtio_net_link_up(VIRTIO_NET_DEVICE *dev)
{
    if (!dev || !dev->Ready)
        return 0;
    if ((dev->FeaturesLow & VIRTIO_NET_F_STATUS) != 0U) {
        uint32_t st = 0U;
        if (mmio_read_private(&dev->Transport, VIRTIO_NET_REGION_DEVICE, NET_CFG_STATUS, 2U, &st))
            dev->NetDevice.LinkUp = (st & VIRTIO_NET_S_LINK_UP) != 0U ? 1U : 0U;
    }
    return dev->NetDevice.LinkUp != 0U;
}
