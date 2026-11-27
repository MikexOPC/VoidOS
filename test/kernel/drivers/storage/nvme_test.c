#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kernel/drivers/storage/nvme/nvme.h"

#define CHECK(cond) do { if (!(cond)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #cond); exit(1); } } while (0)

#define MOCK_SECTORS 64U

typedef struct {
    uint32_t  Cc;
    uint32_t  Csts;
    uint32_t  Aqa;
    uint64_t  Asq;
    uint64_t  Acq;
    uint64_t  IoSq;
    uint64_t  IoCq;
    uint8_t  *DmaVirt;
    uint64_t  DmaPhys;
    uint16_t  AdminSqHead;
    uint16_t  AdminCqTail;
    uint8_t   AdminCqPhase;
    uint16_t  IoSqHead;
    uint16_t  IoCqTail;
    uint8_t   IoCqPhase;
    uint32_t  FlushCount;
    uint8_t   Disk[MOCK_SECTORS * 512U];
} MOCK_NVME;

static uint8_t *phys_to_virt(MOCK_NVME *m, uint64_t phys)
{
    if (phys < m->DmaPhys || phys >= m->DmaPhys + NVME_DMA_PAGES * 4096ULL)
        return NULL;
    return m->DmaVirt + (phys - m->DmaPhys);
}

static void post_cqe(MOCK_NVME *m, uint16_t qid, uint16_t cid, uint16_t status_sc)
{
    uint64_t cq_phys = (qid == 0U) ? m->Acq : m->IoCq;
    uint16_t *tail = (qid == 0U) ? &m->AdminCqTail : &m->IoCqTail;
    uint8_t *phase = (qid == 0U) ? &m->AdminCqPhase : &m->IoCqPhase;
    uint8_t *cq = phys_to_virt(m, cq_phys);
    CHECK(cq != NULL);
    uint8_t *e = cq + (*tail) * 16U;
    memset(e, 0, 16U);
    e[12] = (uint8_t)(cid & 0xFFU);
    e[13] = (uint8_t)(cid >> 8);
    uint16_t st = (uint16_t)((status_sc << 1) | (*phase & 1U));
    e[14] = (uint8_t)(st & 0xFFU);
    e[15] = (uint8_t)(st >> 8);
    *tail = (uint16_t)((*tail + 1U) % NVME_QUEUE_DEPTH);
    if (*tail == 0U)
        *phase ^= 1U;
}

static void process_sq(MOCK_NVME *m, uint16_t qid, uint32_t new_tail)
{
    uint64_t sq_phys = (qid == 0U) ? m->Asq : m->IoSq;
    uint16_t *head = (qid == 0U) ? &m->AdminSqHead : &m->IoSqHead;
    uint8_t *sq = phys_to_virt(m, sq_phys);
    CHECK(sq != NULL);

    while (*head != new_tail) {
        const uint8_t *e = sq + (*head) * 64U;
        uint32_t cdw0 = *(const uint32_t *)(e + 0);
        uint8_t op = (uint8_t)(cdw0 & 0xFFU);
        uint16_t cid = (uint16_t)(cdw0 >> 16);
        uint64_t prp1 = *(const uint64_t *)(e + 24);
        uint32_t cdw10 = *(const uint32_t *)(e + 40);
        uint32_t cdw11 = *(const uint32_t *)(e + 44);
        uint32_t cdw12 = *(const uint32_t *)(e + 48);

        if (qid == 0U) {
            if (op == NVME_ADM_IDENTIFY) {
                uint8_t *buf = phys_to_virt(m, prp1);
                CHECK(buf != NULL);
                memset(buf, 0, 4096U);
                if (cdw10 == 0U) {
                    uint64_t sz = MOCK_SECTORS;
                    memcpy(buf, &sz, 8U);
                    buf[26] = 0U;
                    buf[128 + 2] = 9U; // 2^9 = 512 bytes
                }
                post_cqe(m, 0U, cid, 0U);
            } else if (op == NVME_ADM_CREATE_IO_CQ) {
                m->IoCq = prp1;
                post_cqe(m, 0U, cid, 0U);
            } else if (op == NVME_ADM_CREATE_IO_SQ) {
                m->IoSq = prp1;
                post_cqe(m, 0U, cid, 0U);
            } else {
                post_cqe(m, 0U, cid, 1U);
            }
        } else {
            uint64_t lba = (uint64_t)cdw10 | ((uint64_t)cdw11 << 32);
            uint32_t nlb = (cdw12 & 0xFFFFU) + 1U;
            if (op == NVME_NVM_READ) {
                uint8_t *buf = phys_to_virt(m, prp1);
                CHECK(buf != NULL && lba + nlb <= MOCK_SECTORS);
                memcpy(buf, m->Disk + lba * 512U, nlb * 512U);
                post_cqe(m, 1U, cid, 0U);
            } else if (op == NVME_NVM_WRITE) {
                uint8_t *buf = phys_to_virt(m, prp1);
                CHECK(buf != NULL && lba + nlb <= MOCK_SECTORS);
                memcpy(m->Disk + lba * 512U, buf, nlb * 512U);
                post_cqe(m, 1U, cid, 0U);
            } else if (op == NVME_NVM_FLUSH) {
                ++m->FlushCount;
                post_cqe(m, 1U, cid, 0U);
            } else {
                post_cqe(m, 1U, cid, 1U);
            }
        }
        *head = (uint16_t)((*head + 1U) % NVME_QUEUE_DEPTH);
    }
}

static int mock_r32(void *ctx, uint32_t off, uint32_t *out)
{
    MOCK_NVME *m = (MOCK_NVME *)ctx;
    if (off == NVME_REG_VS) *out = 0x00010400U;
    else if (off == NVME_REG_CC) *out = m->Cc;
    else if (off == NVME_REG_CSTS) *out = m->Csts;
    else if (off == NVME_REG_AQA) *out = m->Aqa;
    else *out = 0U;
    return 1;
}

static int mock_w32(void *ctx, uint32_t off, uint32_t val)
{
    MOCK_NVME *m = (MOCK_NVME *)ctx;
    if (off == NVME_REG_CC) {
        m->Cc = val;
        if ((val & NVME_CC_EN) != 0U) m->Csts |= NVME_CSTS_RDY;
        else m->Csts &= ~NVME_CSTS_RDY;
        if ((val & NVME_CC_SHN_NORMAL) != 0U) m->Csts = (m->Csts & ~NVME_CSTS_SHST_MASK) | NVME_CSTS_SHST_DONE;
    } else if (off == NVME_REG_AQA) {
        m->Aqa = val;
    } else if (off == NVME_REG_DBS) {
        process_sq(m, 0U, val);
    } else if (off == NVME_REG_DBS + 8U) {
        process_sq(m, 1U, val);
    }
    return 1;
}

static int mock_r64(void *ctx, uint32_t off, uint64_t *out)
{
    MOCK_NVME *m = (MOCK_NVME *)ctx;
    if (off == NVME_REG_CAP) *out = (uint64_t)(63U) | (10ULL << 24); // MQES=63, TO=10, DSTRD=0, MPSMIN=0
    else if (off == NVME_REG_ASQ) *out = m->Asq;
    else if (off == NVME_REG_ACQ) *out = m->Acq;
    else *out = 0ULL;
    return 1;
}

static int mock_w64(void *ctx, uint32_t off, uint64_t val)
{
    MOCK_NVME *m = (MOCK_NVME *)ctx;
    if (off == NVME_REG_ASQ) m->Asq = val;
    else if (off == NVME_REG_ACQ) m->Acq = val;
    return 1;
}

static void mock_barrier(void *ctx) { (void)ctx; }

int main(void)
{
    static _Alignas(4096) uint8_t dma[NVME_DMA_PAGES * 4096U];
    static MOCK_NVME mock;
    memset(&mock, 0, sizeof(mock));
    mock.DmaVirt = dma;
    mock.DmaPhys = 0x10000000ULL;
    mock.AdminCqPhase = 1U;
    mock.IoCqPhase = 1U;

    NVME_TRANSPORT tr = {
        &mock, mock_r32, mock_w32, mock_r64, mock_w64, NULL, mock_barrier, NULL, NULL, 0U, 0U, 0U
    };
    KERNEL_NVME_DRIVER drv;
    CHECK(kernel_nvme_init(&drv, &tr, dma, mock.DmaPhys) == KERNEL_STORAGE_OK);
    CHECK(drv.Ready == 1U && drv.TotalSectors == MOCK_SECTORS && drv.SectorBytes == 512U);
    CHECK(drv.BlockDevice.Kind == KERNEL_BLOCK_KIND_NVME);

    // Multi-sector write (10 sectors > 8-sector 4 KiB bounce chunk -> exercises chunking!)
    uint8_t wbuf[10 * 512U];
    uint8_t rbuf[10 * 512U];
    for (uint32_t i = 0; i < sizeof(wbuf); ++i)
        wbuf[i] = (uint8_t)((i * 37U + 11U) & 0xFFU);

    CHECK(drv.BlockDevice.Write(&drv.BlockDevice, 4U, 10U, wbuf) == KERNEL_STORAGE_OK);
    CHECK(drv.BlockDevice.Flush(&drv.BlockDevice) == KERNEL_STORAGE_OK && mock.FlushCount == 1U);
    memset(rbuf, 0, sizeof(rbuf));
    CHECK(drv.BlockDevice.Read(&drv.BlockDevice, 4U, 10U, rbuf) == KERNEL_STORAGE_OK);
    CHECK(memcmp(wbuf, rbuf, sizeof(wbuf)) == 0);

    // Out-of-range must fail cleanly
    CHECK(drv.BlockDevice.Read(&drv.BlockDevice, MOCK_SECTORS, 1U, rbuf) == KERNEL_STORAGE_ERR_RANGE);
    CHECK(drv.BlockDevice.Write(&drv.BlockDevice, MOCK_SECTORS - 2U, 4U, wbuf) == KERNEL_STORAGE_ERR_RANGE);

    kernel_nvme_shutdown(&drv);
    CHECK(drv.Ready == 0U && ((mock.Csts & NVME_CSTS_SHST_MASK) == NVME_CSTS_SHST_DONE));
    printf("PASS: nvme_host (init, identify, I/O queues, multi-chunk R/W, flush, bounds, shutdown)\n");
    return 0;
}
