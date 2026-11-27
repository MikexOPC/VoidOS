#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "kernel/drivers/net/virtio_net/virtio_net.h"

typedef struct {
    uint32_t  DeviceStatus;
    uint32_t  DevFeatSelect;
    uint32_t  DrvFeatSelect;
    uint32_t  DrvFeatLow;
    uint32_t  DrvFeatHigh;
    uint16_t  QueueSelect;
    uint16_t  QueueSize[2];
    uint16_t  QueueEnable[2];
    uint32_t  NotifyCount[2];
    uint8_t   Mac[6];
    uint16_t  LinkStatus;
    uint32_t  Clock;
} MOCK_VNET;

// ====== mock_mmio ======

static int mock_mmio(void *ctx, uint32_t region, uint32_t offset, uint32_t width, int write, uint32_t *val)
{
    MOCK_VNET *m = (MOCK_VNET *)ctx;
    (void)width;
    if (region == VIRTIO_NET_REGION_COMMON) {
        if (offset == 0x00U && write) { m->DevFeatSelect = *val; return 1; }
        if (offset == 0x04U && !write) {
            *val = (m->DevFeatSelect == 0U) ? (VIRTIO_NET_F_MAC | VIRTIO_NET_F_STATUS) : VIRTIO_NET_F_VERSION_1_HI;
            return 1;
        }
        if (offset == 0x08U && write) { m->DrvFeatSelect = *val; return 1; }
        if (offset == 0x0CU && write) {
            if (m->DrvFeatSelect == 0U) m->DrvFeatLow = *val;
            else m->DrvFeatHigh = *val;
            return 1;
        }
        if (offset == 0x10U && write) return 1;
        if (offset == 0x12U && !write) { *val = 2U; return 1; }
        if (offset == 0x14U) {
            if (write) m->DeviceStatus = *val & 0xFFU;
            else *val = m->DeviceStatus;
            return 1;
        }
        if (offset == 0x15U && !write) { *val = 1U; return 1; }
        if (offset == 0x16U && write) { m->QueueSelect = (uint16_t)*val; return 1; }
        if (offset == 0x18U) {
            if (m->QueueSelect >= 2U) return 0;
            if (write) m->QueueSize[m->QueueSelect] = (uint16_t)*val;
            else *val = 64U;
            return 1;
        }
        if (offset == 0x1AU && write) return 1;
        if (offset == 0x1CU && write) {
            if (m->QueueSelect >= 2U) return 0;
            m->QueueEnable[m->QueueSelect] = (uint16_t)*val;
            return 1;
        }
        if (offset == 0x1EU && !write) { *val = m->QueueSelect; return 1; }
        if (offset >= 0x20U && offset <= 0x34U && write) return 1;
        return 0;
    }
    if (region == VIRTIO_NET_REGION_NOTIFY && write) {
        if (*val < 2U) ++m->NotifyCount[*val];
        return 1;
    }
    if (region == VIRTIO_NET_REGION_DEVICE && !write) {
        if (offset < 6U) { *val = m->Mac[offset]; return 1; }
        if (offset == 6U) { *val = m->LinkStatus; return 1; }
        return 0;
    }
    return 0;
}

// ====== mock_clock ======

static int mock_clock(void *ctx, uint32_t *out)
{
    MOCK_VNET *m = (MOCK_VNET *)ctx;
    *out = ++m->Clock;
    return 1;
}

// ====== mock_barrier ======

static void mock_barrier(void *ctx)
{
    (void)ctx;
}

// ====== test_pci_probe ======

static void test_pci_probe(void)
{
    uint8_t cfg[256];
    memset(cfg, 0, sizeof(cfg));
    // Vendor 0x1AF4, Device 0x1041 (modern VirtIO-net), Status cap list bit 4, Cap ptr 0x40.
    cfg[0x00] = 0xF4; cfg[0x01] = 0x1A;
    cfg[0x02] = 0x41; cfg[0x03] = 0x10;
    cfg[0x06] = 0x10;
    cfg[0x34] = 0x40;
    // BAR4 64-bit MMIO at 0xFE000000.
    cfg[0x20] = 0x04; cfg[0x23] = 0xFE;

    // 4 VirtIO PCI capabilities at 0x40, 0x54, 0x68, 0x7C.
    uint32_t offsets[4] = {0x40, 0x54, 0x68, 0x7C};
    for (uint32_t i = 0; i < 4U; ++i) {
        uint32_t p = offsets[i];
        cfg[p + 0] = 0x09;
        cfg[p + 1] = (i < 3U) ? (uint8_t)offsets[i + 1U] : 0U;
        cfg[p + 2] = 20;
        cfg[p + 3] = (uint8_t)(i + 1U);
        cfg[p + 4] = 4;
        cfg[p + 8] = (uint8_t)(i * 0x10);
        cfg[p + 9] = 0x10;
        cfg[p + 12] = 0x00;
        cfg[p + 13] = 0x10;
        if (i == 1U) cfg[p + 16] = 4;
    }

    VIRTIO_NET_PCI_LAYOUT layout;
    assert(kernel_virtio_net_pci_probe(cfg, sizeof(cfg), 48U, &layout) == KVNET_PCI_READY);
    assert(layout.Capabilities == 0x0FU);
    assert(layout.Regions[1].Multiplier == 4U);

    // Wrong device ID must return ABSENT.
    cfg[0x02] = 0x42;
    assert(kernel_virtio_net_pci_probe(cfg, sizeof(cfg), 48U, &layout) == KVNET_PCI_ABSENT);
}

// ====== test_init_tx_rx ======

static void test_init_tx_rx(void)
{
    MOCK_VNET mock;
    memset(&mock, 0, sizeof(mock));
    mock.Mac[0] = 0x52; mock.Mac[1] = 0x54; mock.Mac[2] = 0x00;
    mock.Mac[3] = 0xAA; mock.Mac[4] = 0xBB; mock.Mac[5] = 0xCC;
    mock.LinkStatus = VIRTIO_NET_S_LINK_UP;

    uint8_t *raw = (uint8_t *)calloc(VIRTIO_NET_DMA_PAGES + 1U, 4096U);
    assert(raw != NULL);
    uintptr_t aligned = ((uintptr_t)raw + 4095U) & ~(uintptr_t)4095U;
    uint8_t *dma = (uint8_t *)aligned;

    VIRTIO_NET_TRANSPORT tr = {
        &mock, mock_mmio, mock_clock, mock_barrier,
        0xFFFFFFFFU, 10000U, 10000U, NULL, NULL, 0U
    };
    VIRTIO_NET_DEVICE dev;
    assert(kernel_virtio_net_init(&dev, &tr, dma, 0x00200000ULL) == KERNEL_NET_OK);
    assert(dev.Ready == 1U);
    assert(dev.NetDevice.Mac[5] == 0xCC);
    assert(kernel_virtio_net_link_up(&dev) == 1);
    assert(dev.RxQueue->AvailIdx == VIRTIO_NET_QUEUE_SIZE);
    assert(mock.NotifyCount[VIRTIO_NET_QUEUE_RX] == 1U);

    // TX frame: verify 12-byte zeroed virtio_net_hdr_v1 + Ethernet payload.
    uint8_t frame[60];
    memset(frame, 0x5A, sizeof(frame));
    assert(kernel_virtio_net_send(&dev, frame, sizeof(frame)) == KERNEL_NET_OK);
    assert(dev.TxQueue->AvailIdx == 1U);
    assert(mock.NotifyCount[VIRTIO_NET_QUEUE_TX] == 1U);
    assert(dev.TxQueue->Descriptors[0].Length == sizeof(VIRTIO_NET_HDR_V1) + sizeof(frame));
    for (size_t i = 0; i < sizeof(VIRTIO_NET_HDR_V1); ++i)
        assert(dev.TxBuffers[i] == 0U);
    assert(memcmp(dev.TxBuffers + sizeof(VIRTIO_NET_HDR_V1), frame, sizeof(frame)) == 0);

    // Oversized & undersized frames rejected.
    assert(kernel_virtio_net_send(&dev, frame, 10U) == KERNEL_NET_ERR_PARAM);
    assert(kernel_virtio_net_send(&dev, frame, KERNEL_NET_MAX_FRAME_BYTES + 1U) == KERNEL_NET_ERR_PARAM);

    // Simulate incoming RX frame on slot 0.
    uint8_t rx_out[1600];
    uint32_t rx_len = 0U;
    assert(kernel_virtio_net_poll_rx(&dev, rx_out, sizeof(rx_out), &rx_len) == KERNEL_NET_ERR_EMPTY);

    uint8_t *rx_slot0 = dev.RxBuffers;
    memset(rx_slot0, 0, sizeof(VIRTIO_NET_HDR_V1));
    memset(rx_slot0 + sizeof(VIRTIO_NET_HDR_V1), 0xA5, 42U);
    dev.RxQueue->UsedRing[0].Id = 0U;
    dev.RxQueue->UsedRing[0].Length = (uint32_t)(sizeof(VIRTIO_NET_HDR_V1) + 42U);
    dev.RxQueue->UsedIdx = 1U;

    assert(kernel_virtio_net_poll_rx(&dev, rx_out, sizeof(rx_out), &rx_len) == KERNEL_NET_OK);
    assert(rx_len == 42U);
    assert(rx_out[0] == 0xA5 && rx_out[41] == 0xA5);
    assert(dev.RxQueue->AvailIdx == VIRTIO_NET_QUEUE_SIZE + 1U);
    assert(dev.NetDevice.RxPackets == 1U);

    free(raw);
}

int main(void)
{
    test_pci_probe();
    test_init_tx_rx();
    return 0;
}
