#include "xhci.h"

// USB Mass Storage Class Bulk-Only Transport Rev. 1.0 §5.1-§5.2 & SCSI SBC-3.

#define MSC_CBW_SIGNATURE 0x43425355U // 'USBC'
#define MSC_CSW_SIGNATURE 0x53425355U // 'USBS'
#define MSC_CBW_BYTES     31U
#define MSC_CSW_BYTES     13U
#define MSC_MAX_IO_BYTES  4096U

#define SCSI_OP_TEST_UNIT_READY 0x00U
#define SCSI_OP_READ_CAPACITY10 0x25U
#define SCSI_OP_READ10          0x28U
#define SCSI_OP_WRITE10         0x2AU

// ====== zero_mem_private ======

static void zero_mem_private(void *ptr, size_t bytes)
{
    volatile uint8_t *p = (volatile uint8_t *)ptr;
    for (size_t i = 0; i < bytes; ++i)
        p[i] = 0U;
}

// ====== copy_mem_private ======

static void copy_mem_private(void *dst, const void *src, size_t bytes)
{
    volatile uint8_t *d = (volatile uint8_t *)dst;
    const volatile uint8_t *s = (const volatile uint8_t *)src;
    for (size_t i = 0; i < bytes; ++i)
        d[i] = s[i];
}

// ====== kernel_xhci_parse_msc_config ======

// USB 2.0 §9.6.3-§9.6.6 & USB MSC BOT 1.0 §4.1: Class 0x08, SubClass 0x06 (SCSI), Proto 0x50 (BOT).
int kernel_xhci_parse_msc_config(const uint8_t *desc, uint32_t len, XHCI_MSC_INFO *out)
{
    if (!desc || !out || len < 9U)
        return 0;
    if (desc[0] < 9U || desc[1] != USB_DESC_CONFIG)
        return 0;

    uint32_t total = (uint32_t)desc[2] | ((uint32_t)desc[3] << 8);
    if (total > len)
        total = len;

    uint8_t config_val = desc[5];
    uint8_t cur_iface = 0U;
    int in_msc = 0;
    uint8_t in_addr = 0U, out_addr = 0U;
    uint16_t in_mps = 0U, out_mps = 0U;

    uint32_t pos = 0U;
    while (pos + 2U <= total) {
        uint8_t b_len = desc[pos];
        uint8_t b_type = desc[pos + 1U];
        if (b_len < 2U || pos + b_len > total)
            break;
        if (b_type == USB_DESC_INTERFACE && b_len >= 9U) {
            uint8_t iclass = desc[pos + 5U];
            uint8_t isub = desc[pos + 6U];
            uint8_t iproto = desc[pos + 7U];
            if (iclass == USB_CLASS_MSC && isub == USB_SUBCLASS_SCSI && iproto == USB_PROTO_BOT) {
                cur_iface = desc[pos + 2U];
                in_msc = 1;
                in_addr = 0U;
                out_addr = 0U;
                in_mps = 0U;
                out_mps = 0U;
            } else {
                in_msc = 0;
            }
        } else if (b_type == USB_DESC_ENDPOINT && b_len >= 7U && in_msc) {
            uint8_t ep_addr = desc[pos + 2U];
            uint8_t ep_attr = desc[pos + 3U];
            uint16_t mps = (uint16_t)((uint16_t)desc[pos + 4U] | ((uint16_t)desc[pos + 5U] << 8));
            if ((ep_attr & 0x03U) == 0x02U && mps >= 8U && mps <= 1024U) {
                if (ep_addr & 0x80U) {
                    in_addr = ep_addr;
                    in_mps = mps;
                } else {
                    out_addr = ep_addr;
                    out_mps = mps;
                }
                if (in_addr && out_addr) {
                    out->ConfigValue = config_val;
                    out->InterfaceNumber = cur_iface;
                    out->BulkInAddr = in_addr;
                    out->BulkOutAddr = out_addr;
                    out->BulkInMps = in_mps;
                    out->BulkOutMps = out_mps;
                    return 1;
                }
            }
        }
        pos += b_len;
    }
    return 0;
}

// ====== bulk_push_private ======

static void bulk_push_private(KERNEL_XHCI_DEVICE *dev, XHCI_TRB *ring, uint64_t ring_phys,
    uint32_t *enq, uint8_t *cycle, uint64_t buf_phys, uint32_t len, uint32_t slot, uint32_t dci)
{
    const KERNEL_XHCI_TRANSPORT *t = &dev->Transport;
    XHCI_TRB *trb = &ring[*enq];
    trb->Parameter = buf_phys;
    trb->Status = len;
    t->Barrier(t->Context);
    // Normal TRB (type 1), ISP (bit 2), IOC (bit 5).
    trb->Control = (XHCI_TRB_NORMAL << 10) | (1U << 2) | (1U << 5) | (uint32_t)(*cycle);

    ++(*enq);
    if (*enq >= XHCI_RING_TRBS - 1U) {
        XHCI_TRB *link = &ring[XHCI_RING_TRBS - 1U];
        link->Parameter = ring_phys;
        link->Status = 0U;
        t->Barrier(t->Context);
        link->Control = (XHCI_TRB_LINK << 10) | (1U << 1) | (uint32_t)(*cycle);
        *enq = 0U;
        *cycle ^= 1U;
    }
    kernel_xhci_ring_doorbell(dev, slot, dci);
}

// ====== bot_transfer_private ======

// USB MSC Bulk-Only Transport 1.0 §5.1 (CBW) & §5.2 (CSW).
static int bot_transfer_private(KERNEL_XHCI_DEVICE *dev, const uint8_t *cdb, uint8_t cdb_len,
    uint32_t data_len, int data_in)
{
    XHCI_MSC_SLOT *msc = &dev->Msc;
    if (!msc->Active || !cdb || !cdb_len || cdb_len > 16U || data_len > MSC_MAX_IO_BYTES)
        return 0;

    uint32_t tag = ++msc->Tag;
    if (!tag)
        tag = ++msc->Tag;

    zero_mem_private(msc->CbwBuf, MSC_CBW_BYTES);
    msc->CbwBuf[0] = 'U';
    msc->CbwBuf[1] = 'S';
    msc->CbwBuf[2] = 'B';
    msc->CbwBuf[3] = 'C';
    msc->CbwBuf[4] = (uint8_t)(tag & 0xFFU);
    msc->CbwBuf[5] = (uint8_t)((tag >> 8) & 0xFFU);
    msc->CbwBuf[6] = (uint8_t)((tag >> 16) & 0xFFU);
    msc->CbwBuf[7] = (uint8_t)((tag >> 24) & 0xFFU);
    msc->CbwBuf[8] = (uint8_t)(data_len & 0xFFU);
    msc->CbwBuf[9] = (uint8_t)((data_len >> 8) & 0xFFU);
    msc->CbwBuf[10] = (uint8_t)((data_len >> 16) & 0xFFU);
    msc->CbwBuf[11] = (uint8_t)((data_len >> 24) & 0xFFU);
    msc->CbwBuf[12] = data_in ? 0x80U : 0x00U;
    msc->CbwBuf[13] = 0U; // LUN 0
    msc->CbwBuf[14] = cdb_len;
    for (uint8_t i = 0; i < cdb_len; ++i)
        msc->CbwBuf[15U + i] = cdb[i];

    // 1) Command Block Wrapper (31 bytes on Bulk OUT)
    bulk_push_private(dev, msc->OutRing, msc->OutRingPhys, &msc->OutEnqueue, &msc->OutCycle,
        msc->CbwBufPhys, MSC_CBW_BYTES, msc->SlotId, msc->OutDci);
    if (!kernel_xhci_wait_transfer(dev, msc->SlotId, msc->OutDci, 2000U))
        return 0;

    // 2) Optional Data Stage (Bulk IN or Bulk OUT)
    if (data_len > 0U) {
        if (data_in) {
            bulk_push_private(dev, msc->InRing, msc->InRingPhys, &msc->InEnqueue, &msc->InCycle,
                msc->DataBufPhys, data_len, msc->SlotId, msc->InDci);
            if (!kernel_xhci_wait_transfer(dev, msc->SlotId, msc->InDci, 2000U))
                return 0;
        } else {
            bulk_push_private(dev, msc->OutRing, msc->OutRingPhys, &msc->OutEnqueue, &msc->OutCycle,
                msc->DataBufPhys, data_len, msc->SlotId, msc->OutDci);
            if (!kernel_xhci_wait_transfer(dev, msc->SlotId, msc->OutDci, 2000U))
                return 0;
        }
    }

    // 3) Command Status Wrapper (13 bytes on Bulk IN)
    zero_mem_private(msc->CswBuf, MSC_CSW_BYTES);
    bulk_push_private(dev, msc->InRing, msc->InRingPhys, &msc->InEnqueue, &msc->InCycle,
        msc->CswBufPhys, MSC_CSW_BYTES, msc->SlotId, msc->InDci);
    if (!kernel_xhci_wait_transfer(dev, msc->SlotId, msc->InDci, 2000U))
        return 0;

    dev->Transport.Barrier(dev->Transport.Context);
    uint32_t sig = (uint32_t)msc->CswBuf[0] |
                   ((uint32_t)msc->CswBuf[1] << 8) |
                   ((uint32_t)msc->CswBuf[2] << 16) |
                   ((uint32_t)msc->CswBuf[3] << 24);
    uint32_t csw_tag = (uint32_t)msc->CswBuf[4] |
                       ((uint32_t)msc->CswBuf[5] << 8) |
                       ((uint32_t)msc->CswBuf[6] << 16) |
                       ((uint32_t)msc->CswBuf[7] << 24);
    uint8_t status = msc->CswBuf[12];
    return sig == MSC_CSW_SIGNATURE && csw_tag == tag && status == 0U;
}

// ====== kernel_xhci_msc_setup ======

int kernel_xhci_msc_setup(KERNEL_XHCI_DEVICE *dev, uint32_t slot_id, uint32_t port_idx,
    uint32_t speed, XHCI_TRB *ep0_ring, uint64_t ep0_phys, uint32_t *ep0_enq,
    uint8_t *ep0_cycle, const XHCI_MSC_INFO *info, uint8_t *dev_ctx, uint64_t dev_ctx_phys)
{
    if (!dev || !info || !slot_id || dev->Msc.Active)
        return 0;
    const KERNEL_XHCI_TRANSPORT *t = &dev->Transport;

    uint32_t in_ep = (uint32_t)(info->BulkInAddr & 0x0FU);
    uint32_t out_ep = (uint32_t)(info->BulkOutAddr & 0x0FU);
    uint32_t in_dci = in_ep * 2U + 1U;
    uint32_t out_dci = out_ep * 2U;
    if (in_dci < 2U || in_dci > 31U || out_dci < 2U || out_dci > 31U)
        return 0;

    // SET_CONFIGURATION (bmReq=0x00, bReq=9, wVal=ConfigValue)
    uint64_t set_cfg = 0x00ULL | (9ULL << 8) | ((uint64_t)info->ConfigValue << 16);
    if (!kernel_xhci_ep0_transfer(dev, slot_id, ep0_ring, ep0_phys, ep0_enq, ep0_cycle,
        set_cfg, 0ULL, 0U, 0))
        return 0;

    XHCI_MSC_SLOT *msc = &dev->Msc;
    if (!msc->InRing)
        msc->InRing = (XHCI_TRB *)t->AllocDmaPage(t->Context, &msc->InRingPhys);
    if (!msc->OutRing)
        msc->OutRing = (XHCI_TRB *)t->AllocDmaPage(t->Context, &msc->OutRingPhys);
    if (!msc->CbwBuf)
        msc->CbwBuf = (uint8_t *)t->AllocDmaPage(t->Context, &msc->CbwBufPhys);
    if (!msc->CswBuf)
        msc->CswBuf = (uint8_t *)t->AllocDmaPage(t->Context, &msc->CswBufPhys);
    if (!msc->DataBuf)
        msc->DataBuf = (uint8_t *)t->AllocDmaPage(t->Context, &msc->DataBufPhys);
    if (!msc->InRing || !msc->OutRing || !msc->CbwBuf || !msc->CswBuf || !msc->DataBuf)
        return 0;

    zero_mem_private(msc->InRing, 4096U);
    zero_mem_private(msc->OutRing, 4096U);
    zero_mem_private(msc->CbwBuf, 4096U);
    zero_mem_private(msc->CswBuf, 4096U);
    zero_mem_private(msc->DataBuf, 4096U);

    // xHCI 1.2 §4.6.6 & §6.2.3.1: Configure Bulk OUT (EP Type=2) and Bulk IN (EP Type=6).
    uint32_t csz = dev->ContextSize;
    uint32_t max_dci = in_dci > out_dci ? in_dci : out_dci;
    uint8_t *in_ctx = dev->InCtx;
    zero_mem_private(in_ctx, 4096U);

    uint32_t *icc = (uint32_t *)in_ctx;
    icc[1] = (1U << 0) | (1U << in_dci) | (1U << out_dci);

    uint32_t *slot_ctx = (uint32_t *)(in_ctx + csz);
    slot_ctx[0] = (max_dci << 27) | (speed << 20);
    slot_ctx[1] = (port_idx + 1U) << 16;

    uint32_t *out_ctx = (uint32_t *)(in_ctx + (out_dci + 1U) * csz);
    out_ctx[1] = (3U << 1) | (2U << 3) | ((uint32_t)info->BulkOutMps << 16); // CErr=3, Bulk OUT(2)
    out_ctx[2] = (uint32_t)(msc->OutRingPhys | 1ULL);
    out_ctx[3] = (uint32_t)(msc->OutRingPhys >> 32);
    out_ctx[4] = (uint32_t)info->BulkOutMps;

    uint32_t *in_ep_ctx = (uint32_t *)(in_ctx + (in_dci + 1U) * csz);
    in_ep_ctx[1] = (3U << 1) | (6U << 3) | ((uint32_t)info->BulkInMps << 16); // CErr=3, Bulk IN(6)
    in_ep_ctx[2] = (uint32_t)(msc->InRingPhys | 1ULL);
    in_ep_ctx[3] = (uint32_t)(msc->InRingPhys >> 32);
    in_ep_ctx[4] = (uint32_t)info->BulkInMps;

    XHCI_TRB ev = {0};
    if (!kernel_xhci_submit_cmd(dev, dev->InCtxPhys, 0U,
        (XHCI_TRB_CONFIGURE_EP << 10) | (slot_id << 24), &ev))
        return 0;

    msc->Active = 1U;
    msc->SlotId = (uint8_t)slot_id;
    msc->PortId = (uint8_t)(port_idx + 1U);
    msc->InDci = (uint8_t)in_dci;
    msc->OutDci = (uint8_t)out_dci;
    msc->InCycle = 1U;
    msc->OutCycle = 1U;
    msc->InEnqueue = 0U;
    msc->OutEnqueue = 0U;
    msc->DevCtx = dev_ctx;
    msc->DevCtxPhys = dev_ctx_phys;
    msc->Ep0Ring = ep0_ring;
    msc->Ep0RingPhys = ep0_phys;

    // SCSI TEST UNIT READY (0x00): up to 3 attempts to clear initial Unit Attention.
    uint8_t tur[6] = {SCSI_OP_TEST_UNIT_READY, 0, 0, 0, 0, 0};
    for (uint32_t attempt = 0; attempt < 3U; ++attempt) {
        if (bot_transfer_private(dev, tur, 6U, 0U, 0))
            break;
    }

    // SCSI READ CAPACITY(10) (0x25): 8-byte response [last_lba BE32, block_size BE32].
    uint8_t rcap[10] = {SCSI_OP_READ_CAPACITY10, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    if (!bot_transfer_private(dev, rcap, 10U, 8U, 1)) {
        msc->Active = 0U;
        return 0;
    }

    uint32_t last_lba = ((uint32_t)msc->DataBuf[0] << 24) |
                        ((uint32_t)msc->DataBuf[1] << 16) |
                        ((uint32_t)msc->DataBuf[2] << 8) |
                        (uint32_t)msc->DataBuf[3];
    uint32_t block_size = ((uint32_t)msc->DataBuf[4] << 24) |
                          ((uint32_t)msc->DataBuf[5] << 16) |
                          ((uint32_t)msc->DataBuf[6] << 8) |
                          (uint32_t)msc->DataBuf[7];
    if (block_size != 512U || last_lba == 0U || last_lba == 0xFFFFFFFFU) {
        msc->Active = 0U;
        return 0;
    }

    msc->BlockSize = block_size;
    msc->TotalSectors = (uint64_t)last_lba + 1ULL;
    return 1;
}

// ====== kernel_xhci_msc_read_sectors ======

int kernel_xhci_msc_read_sectors(KERNEL_XHCI_DEVICE *dev, uint64_t lba, uint32_t count, void *buffer)
{
    if (!dev || !dev->Ready || !dev->Msc.Active || !buffer || !count)
        return 0;
    XHCI_MSC_SLOT *msc = &dev->Msc;
    if (msc->BlockSize != 512U || lba >= msc->TotalSectors || count > msc->TotalSectors - lba ||
        lba + count - 1ULL > 0xFFFFFFFFULL)
        return 0;

    uint8_t *dst = (uint8_t *)buffer;
    uint32_t remaining = count;
    uint64_t cur_lba = lba;
    while (remaining > 0U) {
        uint32_t chunk = remaining > 8U ? 8U : remaining; // 8 * 512 = 4096 bytes
        uint8_t cdb[10] = {
            SCSI_OP_READ10, 0U,
            (uint8_t)((cur_lba >> 24) & 0xFFU),
            (uint8_t)((cur_lba >> 16) & 0xFFU),
            (uint8_t)((cur_lba >> 8) & 0xFFU),
            (uint8_t)(cur_lba & 0xFFU),
            0U,
            (uint8_t)((chunk >> 8) & 0xFFU),
            (uint8_t)(chunk & 0xFFU),
            0U
        };
        if (!bot_transfer_private(dev, cdb, 10U, chunk * 512U, 1))
            return 0;
        copy_mem_private(dst, msc->DataBuf, (size_t)chunk * 512U);
        dst += (size_t)chunk * 512U;
        cur_lba += chunk;
        remaining -= chunk;
    }
    return 1;
}

// ====== kernel_xhci_msc_write_sectors ======

int kernel_xhci_msc_write_sectors(KERNEL_XHCI_DEVICE *dev, uint64_t lba, uint32_t count, const void *buffer)
{
    if (!dev || !dev->Ready || !dev->Msc.Active || !buffer || !count)
        return 0;
    XHCI_MSC_SLOT *msc = &dev->Msc;
    if (msc->BlockSize != 512U || lba >= msc->TotalSectors || count > msc->TotalSectors - lba ||
        lba + count - 1ULL > 0xFFFFFFFFULL)
        return 0;

    const uint8_t *src = (const uint8_t *)buffer;
    uint32_t remaining = count;
    uint64_t cur_lba = lba;
    while (remaining > 0U) {
        uint32_t chunk = remaining > 8U ? 8U : remaining;
        copy_mem_private(msc->DataBuf, src, (size_t)chunk * 512U);
        uint8_t cdb[10] = {
            SCSI_OP_WRITE10, 0U,
            (uint8_t)((cur_lba >> 24) & 0xFFU),
            (uint8_t)((cur_lba >> 16) & 0xFFU),
            (uint8_t)((cur_lba >> 8) & 0xFFU),
            (uint8_t)(cur_lba & 0xFFU),
            0U,
            (uint8_t)((chunk >> 8) & 0xFFU),
            (uint8_t)(chunk & 0xFFU),
            0U
        };
        if (!bot_transfer_private(dev, cdb, 10U, chunk * 512U, 0))
            return 0;
        src += (size_t)chunk * 512U;
        cur_lba += chunk;
        remaining -= chunk;
    }
    return 1;
}
