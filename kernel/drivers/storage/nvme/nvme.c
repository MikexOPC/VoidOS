#include "nvme.h"

// ====== nvme_sqe_and_cqe ======

// NVM Express Base Specification Rev. 2.0c §4.2 (64-byte SQE) and §4.6 (16-byte CQE).
typedef struct {
    uint32_t  Cdw0;
    uint32_t  Nsid;
    uint64_t  Reserved;
    uint64_t  Mptr;
    uint64_t  Prp1;
    uint64_t  Prp2;
    uint32_t  Cdw10;
    uint32_t  Cdw11;
    uint32_t  Cdw12;
    uint32_t  Cdw13;
    uint32_t  Cdw14;
    uint32_t  Cdw15;
} NVME_SQE;

typedef struct {
    uint32_t  Dw0;
    uint32_t  Dw1;
    uint16_t  SqHead;
    uint16_t  SqId;
    uint16_t  Cid;
    uint16_t  Status;
} NVME_CQE;

_Static_assert(sizeof(NVME_SQE) == 64U, "NVMe SQE size");
_Static_assert(sizeof(NVME_CQE) == 16U, "NVMe CQE size");

// ====== zero_and_copy ======

static void zero_bytes_private(void *ptr, uint32_t len)
{
    uint8_t *dst = (uint8_t *)ptr;
    for (uint32_t i = 0; i < len; ++i)
        dst[i] = 0U;
}

static void copy_bytes_private(void *dst_p, const void *src_p, uint32_t len)
{
    uint8_t *dst = (uint8_t *)dst_p;
    const uint8_t *src = (const uint8_t *)src_p;
    for (uint32_t i = 0; i < len; ++i)
        dst[i] = src[i];
}

// ====== clock_helpers ======

static uint32_t ticks_for_ms_private(const NVME_TRANSPORT *t, uint32_t ms)
{
    if (!t->ClockHz)
        return 1U;
    uint64_t ticks = ((uint64_t)ms * (uint64_t)t->ClockHz) / 1000ULL;
    return ticks ? (uint32_t)ticks : 1U;
}

static int clock_expired_private(const NVME_TRANSPORT *t, uint32_t start, uint32_t limit, uint32_t spin)
{
    if (t->ClockRead && t->ClockHz) {
        uint32_t now = 0U;
        if (t->ClockRead(t->Context, &now))
            return ((now - start) & t->ClockMask) >= limit;
    }
    return spin >= 2000000U;
}

// ====== doorbell_offsets ======

// NVMe Base Spec Rev. 2.0c §3.1.26 & §3.1.27.
static uint32_t sq_doorbell_private(const KERNEL_NVME_DRIVER *drv, uint16_t qid)
{
    return NVME_REG_DBS + (uint32_t)(2U * qid) * drv->DoorbellStride;
}

static uint32_t cq_doorbell_private(const KERNEL_NVME_DRIVER *drv, uint16_t qid)
{
    return NVME_REG_DBS + (uint32_t)(2U * qid + 1U) * drv->DoorbellStride;
}

// ====== wait_csts_ready_private ======

static int wait_csts_ready_private(KERNEL_NVME_DRIVER *drv, uint32_t want_ready, uint32_t timeout_ms)
{
    const NVME_TRANSPORT *t = &drv->Transport;
    uint32_t start = 0U;
    if (t->ClockRead)
        (void)t->ClockRead(t->Context, &start);
    uint32_t limit = ticks_for_ms_private(t, timeout_ms);

    for (uint32_t spin = 0; !clock_expired_private(t, start, limit, spin); ++spin) {
        uint32_t csts = 0U;
        if (!t->Read32(t->Context, NVME_REG_CSTS, &csts))
            return KERNEL_STORAGE_ERR_IO;
        if ((csts & NVME_CSTS_CFS) != 0U)
            return KERNEL_STORAGE_ERR_IO;
        if (((csts & NVME_CSTS_RDY) != 0U) == (want_ready != 0U))
            return KERNEL_STORAGE_OK;
    }
    return KERNEL_STORAGE_ERR_TIMEOUT;
}

// ====== submit_and_wait_private ======

static int submit_and_wait_private(
    KERNEL_NVME_DRIVER *drv,
    uint16_t qid,
    const NVME_SQE *cmd_in,
    uint32_t timeout_ms
)
{
    const NVME_TRANSPORT *t = &drv->Transport;
    uint32_t sq_page = (qid == 0U) ? 0U : 2U;
    uint32_t cq_page = (qid == 0U) ? 1U : 3U;
    volatile NVME_SQE *sq = (volatile NVME_SQE *)(drv->DmaVirt + sq_page * 4096U);
    volatile NVME_CQE *cq = (volatile NVME_CQE *)(drv->DmaVirt + cq_page * 4096U);

    uint16_t *sq_tail = (qid == 0U) ? &drv->AdminSqTail : &drv->IoSqTail;
    uint16_t *cq_head = (qid == 0U) ? &drv->AdminCqHead : &drv->IoCqHead;
    uint8_t *cq_phase = (qid == 0U) ? &drv->AdminCqPhase : &drv->IoCqPhase;

    uint16_t cid = ++drv->NextCid;
    if (!cid)
        cid = ++drv->NextCid;

    NVME_SQE cmd = *cmd_in;
    cmd.Cdw0 = (cmd.Cdw0 & 0x0000FFFFU) | ((uint32_t)cid << 16);

    uint16_t slot = *sq_tail;
    sq[slot] = cmd;
    *sq_tail = (uint16_t)((slot + 1U) % NVME_QUEUE_DEPTH);

    uint64_t irq_before = (t->IrqEnabled && t->IrqSeq) ? t->IrqSeq(t->Context) : 0ULL;
    if (t->Barrier)
        t->Barrier(t->Context);

    if (!t->Write32(t->Context, sq_doorbell_private(drv, qid), (uint32_t)(*sq_tail)))
        return KERNEL_STORAGE_ERR_IO;

    uint32_t start = 0U;
    if (t->ClockRead)
        (void)t->ClockRead(t->Context, &start);
    uint32_t limit = ticks_for_ms_private(t, timeout_ms);

    for (uint32_t spin = 0; !clock_expired_private(t, start, limit, spin); ++spin) {
        if (t->Barrier)
            t->Barrier(t->Context);
        uint16_t head = *cq_head;
        uint16_t status_word = cq[head].Status;
        uint8_t phase = (uint8_t)(status_word & 1U);
        if (phase == *cq_phase) {
            uint16_t done_cid = cq[head].Cid;
            uint16_t sc_sct = (uint16_t)(status_word >> 1);
            *cq_head = (uint16_t)((head + 1U) % NVME_QUEUE_DEPTH);
            if (*cq_head == 0U)
                *cq_phase = (uint8_t)(*cq_phase ^ 1U);
            (void)t->Write32(t->Context, cq_doorbell_private(drv, qid), (uint32_t)(*cq_head));
            if (done_cid != cid || sc_sct != 0U)
                return KERNEL_STORAGE_ERR_IO;
            return KERNEL_STORAGE_OK;
        }
        if (t->IrqEnabled && t->Wait && spin > 16U) {
            t->Wait(t->Context, irq_before + 1ULL);
            if (t->IrqSeq)
                irq_before = t->IrqSeq(t->Context);
        }
    }
    return KERNEL_STORAGE_ERR_TIMEOUT;
}

// ====== nvme_block_read_private ======

static int nvme_block_read_private(
    KERNEL_BLOCK_DEVICE *device,
    uint64_t lba,
    uint32_t sector_count,
    void *buffer
)
{
    if (!device || !device->Context || !buffer || !sector_count)
        return KERNEL_STORAGE_ERR_ARG;
    KERNEL_NVME_DRIVER *drv = (KERNEL_NVME_DRIVER *)device->Context;
    if (!drv->Ready || drv->Self != (uintptr_t)drv)
        return KERNEL_STORAGE_ERR_IO;
    if (lba > drv->TotalSectors || sector_count > drv->TotalSectors - lba)
        return KERNEL_STORAGE_ERR_RANGE;

    uint32_t max_chunk = 4096U / drv->SectorBytes;
    if (!max_chunk)
        max_chunk = 1U;
    uint8_t *dst = (uint8_t *)buffer;
    uint8_t *bounce = drv->DmaVirt + 4U * 4096U;
    uint64_t bounce_phys = drv->DmaPhys + 4ULL * 4096ULL;

    uint32_t done = 0U;
    while (done < sector_count) {
        uint32_t chunk = sector_count - done;
        if (chunk > max_chunk)
            chunk = max_chunk;

        NVME_SQE cmd;
        zero_bytes_private(&cmd, sizeof(cmd));
        cmd.Cdw0 = NVME_NVM_READ;
        cmd.Nsid = drv->Nsid;
        cmd.Prp1 = bounce_phys;
        uint64_t cur_lba = lba + done;
        cmd.Cdw10 = (uint32_t)(cur_lba & 0xFFFFFFFFULL);
        cmd.Cdw11 = (uint32_t)(cur_lba >> 32);
        cmd.Cdw12 = chunk - 1U;

        int rc = submit_and_wait_private(drv, 1U, &cmd, 3000U);
        if (rc != KERNEL_STORAGE_OK)
            return rc;

        uint32_t bytes = chunk * drv->SectorBytes;
        copy_bytes_private(dst + (size_t)done * drv->SectorBytes, bounce, bytes);
        done += chunk;
    }
    return KERNEL_STORAGE_OK;
}

// ====== nvme_block_write_private ======

static int nvme_block_write_private(
    KERNEL_BLOCK_DEVICE *device,
    uint64_t lba,
    uint32_t sector_count,
    const void *buffer
)
{
    if (!device || !device->Context || !buffer || !sector_count)
        return KERNEL_STORAGE_ERR_ARG;
    KERNEL_NVME_DRIVER *drv = (KERNEL_NVME_DRIVER *)device->Context;
    if (!drv->Ready || drv->Self != (uintptr_t)drv)
        return KERNEL_STORAGE_ERR_IO;
    if (lba > drv->TotalSectors || sector_count > drv->TotalSectors - lba)
        return KERNEL_STORAGE_ERR_RANGE;

    uint32_t max_chunk = 4096U / drv->SectorBytes;
    if (!max_chunk)
        max_chunk = 1U;
    const uint8_t *src = (const uint8_t *)buffer;
    uint8_t *bounce = drv->DmaVirt + 4U * 4096U;
    uint64_t bounce_phys = drv->DmaPhys + 4ULL * 4096ULL;

    uint32_t done = 0U;
    while (done < sector_count) {
        uint32_t chunk = sector_count - done;
        if (chunk > max_chunk)
            chunk = max_chunk;

        uint32_t bytes = chunk * drv->SectorBytes;
        copy_bytes_private(bounce, src + (size_t)done * drv->SectorBytes, bytes);

        NVME_SQE cmd;
        zero_bytes_private(&cmd, sizeof(cmd));
        cmd.Cdw0 = NVME_NVM_WRITE;
        cmd.Nsid = drv->Nsid;
        cmd.Prp1 = bounce_phys;
        uint64_t cur_lba = lba + done;
        cmd.Cdw10 = (uint32_t)(cur_lba & 0xFFFFFFFFULL);
        cmd.Cdw11 = (uint32_t)(cur_lba >> 32);
        cmd.Cdw12 = chunk - 1U;

        int rc = submit_and_wait_private(drv, 1U, &cmd, 3000U);
        if (rc != KERNEL_STORAGE_OK)
            return rc;

        done += chunk;
    }
    return KERNEL_STORAGE_OK;
}

// ====== nvme_block_flush_private ======

static int nvme_block_flush_private(KERNEL_BLOCK_DEVICE *device)
{
    if (!device || !device->Context)
        return KERNEL_STORAGE_ERR_ARG;
    KERNEL_NVME_DRIVER *drv = (KERNEL_NVME_DRIVER *)device->Context;
    if (!drv->Ready || drv->Self != (uintptr_t)drv)
        return KERNEL_STORAGE_ERR_IO;

    NVME_SQE cmd;
    zero_bytes_private(&cmd, sizeof(cmd));
    cmd.Cdw0 = NVME_NVM_FLUSH;
    cmd.Nsid = drv->Nsid;
    return submit_and_wait_private(drv, 1U, &cmd, 3000U);
}

// ====== kernel_nvme_init ======

int kernel_nvme_init(
    KERNEL_NVME_DRIVER *drv,
    const NVME_TRANSPORT *transport,
    void *dma_virt,
    uint64_t dma_phys
)
{
    if (!drv || !transport || !dma_virt || !dma_phys || (dma_phys & 4095ULL) ||
        !transport->Read32 || !transport->Write32 || !transport->Read64 || !transport->Write64)
        return KERNEL_STORAGE_ERR_ARG;

    zero_bytes_private(drv, sizeof(*drv));
    zero_bytes_private(dma_virt, NVME_DMA_PAGES * 4096U);

    drv->Self = (uintptr_t)drv;
    drv->Transport = *transport;
    drv->DmaVirt = (uint8_t *)dma_virt;
    drv->DmaPhys = dma_phys;
    drv->AdminCqPhase = 1U;
    drv->IoCqPhase = 1U;
    drv->Nsid = 1U;

    uint64_t cap = 0ULL;
    uint32_t vs = 0U;
    if (!transport->Read64(transport->Context, NVME_REG_CAP, &cap) ||
        !transport->Read32(transport->Context, NVME_REG_VS, &vs))
        return KERNEL_STORAGE_ERR_IO;

    uint32_t mqes = (uint32_t)(cap & 0xFFFFULL) + 1U;
    uint32_t dstrd = (uint32_t)((cap >> 32) & 0xFULL);
    uint32_t mpsmin = (uint32_t)((cap >> 48) & 0xFULL);
    uint32_t to_500ms = (uint32_t)((cap >> 24) & 0xFFULL);
    uint32_t timeout_ms = to_500ms ? (to_500ms * 500U) : 5000U;
    if (timeout_ms < 2000U)
        timeout_ms = 2000U;
    if (timeout_ms > 10000U)
        timeout_ms = 10000U;

    if (mqes < NVME_QUEUE_DEPTH || mpsmin > 0U)
        return KERNEL_STORAGE_ERR_CORRUPT;

    drv->DoorbellStride = 4U << dstrd;
    drv->Version = vs;

    // 1. Disable controller (CC.EN = 0) and wait for CSTS.RDY = 0.
    uint32_t cc = 0U;
    if (!transport->Read32(transport->Context, NVME_REG_CC, &cc))
        return KERNEL_STORAGE_ERR_IO;
    if ((cc & NVME_CC_EN) != 0U) {
        if (!transport->Write32(transport->Context, NVME_REG_CC, cc & ~NVME_CC_EN))
            return KERNEL_STORAGE_ERR_IO;
        int rc = wait_csts_ready_private(drv, 0U, timeout_ms);
        if (rc != KERNEL_STORAGE_OK)
            return rc;
    }

    // 2. Configure Admin Queue Attributes (AQA) and base addresses (ASQ / ACQ).
    uint32_t aqa = ((NVME_QUEUE_DEPTH - 1U) << 16) | (NVME_QUEUE_DEPTH - 1U);
    if (!transport->Write32(transport->Context, NVME_REG_AQA, aqa) ||
        !transport->Write64(transport->Context, NVME_REG_ASQ, dma_phys) ||
        !transport->Write64(transport->Context, NVME_REG_ACQ, dma_phys + 4096ULL))
        return KERNEL_STORAGE_ERR_IO;

    // 3. Enable controller with 4 KiB pages, 64B I/O SQE, 16B I/O CQE.
    uint32_t new_cc = NVME_CC_EN | NVME_CC_IOSQES_64 | NVME_CC_IOCQES_16;
    if (!transport->Write32(transport->Context, NVME_REG_CC, new_cc))
        return KERNEL_STORAGE_ERR_IO;
    int rc = wait_csts_ready_private(drv, 1U, timeout_ms);
    if (rc != KERNEL_STORAGE_OK)
        return rc;

    uint8_t *bounce = drv->DmaVirt + 4U * 4096U;
    uint64_t bounce_phys = drv->DmaPhys + 4ULL * 4096ULL;

    // 4. Identify Controller (CNS = 1).
    NVME_SQE id_ctrl;
    zero_bytes_private(&id_ctrl, sizeof(id_ctrl));
    id_ctrl.Cdw0 = NVME_ADM_IDENTIFY;
    id_ctrl.Prp1 = bounce_phys;
    id_ctrl.Cdw10 = 1U;
    rc = submit_and_wait_private(drv, 0U, &id_ctrl, timeout_ms);
    if (rc != KERNEL_STORAGE_OK)
        return rc;

    // 5. Identify Namespace 1 (CNS = 0, NSID = 1).
    zero_bytes_private(bounce, 4096U);
    NVME_SQE id_ns;
    zero_bytes_private(&id_ns, sizeof(id_ns));
    id_ns.Cdw0 = NVME_ADM_IDENTIFY;
    id_ns.Nsid = 1U;
    id_ns.Prp1 = bounce_phys;
    id_ns.Cdw10 = 0U;
    rc = submit_and_wait_private(drv, 0U, &id_ns, timeout_ms);
    if (rc != KERNEL_STORAGE_OK)
        return rc;

    uint64_t nsze = (uint64_t)bounce[0]
        | ((uint64_t)bounce[1] << 8)
        | ((uint64_t)bounce[2] << 16)
        | ((uint64_t)bounce[3] << 24)
        | ((uint64_t)bounce[4] << 32)
        | ((uint64_t)bounce[5] << 40)
        | ((uint64_t)bounce[6] << 48)
        | ((uint64_t)bounce[7] << 56);
    uint8_t flbas = bounce[26] & 0x0FU;
    uint32_t lbaf_off = 128U + (uint32_t)flbas * 4U;
    uint8_t lbads = bounce[lbaf_off + 2U];
    uint32_t sector_bytes = (lbads >= 9U && lbads <= 12U) ? (1U << lbads) : 512U;
    if (!nsze || sector_bytes != 512U)
        return KERNEL_STORAGE_ERR_CORRUPT;

    drv->TotalSectors = nsze;
    drv->SectorBytes = sector_bytes;

    // 6. Create I/O Completion Queue 1 (PC = 1, IEN = 1 if IRQ enabled).
    NVME_SQE create_cq;
    zero_bytes_private(&create_cq, sizeof(create_cq));
    create_cq.Cdw0 = NVME_ADM_CREATE_IO_CQ;
    create_cq.Prp1 = dma_phys + 3ULL * 4096ULL;
    create_cq.Cdw10 = ((NVME_QUEUE_DEPTH - 1U) << 16) | 1U;
    create_cq.Cdw11 = 1U | (transport->IrqEnabled ? 2U : 0U);
    rc = submit_and_wait_private(drv, 0U, &create_cq, timeout_ms);
    if (rc != KERNEL_STORAGE_OK)
        return rc;

    // 7. Create I/O Submission Queue 1 bound to CQ 1 (PC = 1).
    NVME_SQE create_sq;
    zero_bytes_private(&create_sq, sizeof(create_sq));
    create_sq.Cdw0 = NVME_ADM_CREATE_IO_SQ;
    create_sq.Prp1 = dma_phys + 2ULL * 4096ULL;
    create_sq.Cdw10 = ((NVME_QUEUE_DEPTH - 1U) << 16) | 1U;
    create_sq.Cdw11 = (1U << 16) | 1U;
    rc = submit_and_wait_private(drv, 0U, &create_sq, timeout_ms);
    if (rc != KERNEL_STORAGE_OK)
        return rc;

    drv->Ready = 1U;
    drv->BlockDevice.Self = (uintptr_t)&drv->BlockDevice;
    drv->BlockDevice.Name = "nvme0n1";
    drv->BlockDevice.Context = drv;
    drv->BlockDevice.TotalSectors = drv->TotalSectors;
    drv->BlockDevice.SectorBytes = drv->SectorBytes;
    drv->BlockDevice.MaxTransferSectors = 4096U / drv->SectorBytes;
    drv->BlockDevice.Kind = KERNEL_BLOCK_KIND_NVME;
    drv->BlockDevice.ReadOnly = 0U;
    drv->BlockDevice.Ready = 1U;
    drv->BlockDevice.Read = nvme_block_read_private;
    drv->BlockDevice.Write = nvme_block_write_private;
    drv->BlockDevice.Flush = nvme_block_flush_private;
    return KERNEL_STORAGE_OK;
}

// ====== kernel_nvme_shutdown ======

void kernel_nvme_shutdown(KERNEL_NVME_DRIVER *drv)
{
    if (!drv || drv->Self != (uintptr_t)drv || !drv->Ready)
        return;
    const NVME_TRANSPORT *t = &drv->Transport;
    uint32_t cc = 0U;
    if (t->Read32 && t->Write32 && t->Read32(t->Context, NVME_REG_CC, &cc)) {
        (void)t->Write32(t->Context, NVME_REG_CC, (cc & ~(3U << 14)) | NVME_CC_SHN_NORMAL);
        for (uint32_t i = 0; i < 10000U; ++i) {
            uint32_t csts = 0U;
            if (t->Read32(t->Context, NVME_REG_CSTS, &csts) &&
                ((csts & NVME_CSTS_SHST_MASK) == NVME_CSTS_SHST_DONE))
                break;
        }
    }
    drv->Ready = 0U;
    drv->BlockDevice.Ready = 0U;
}
