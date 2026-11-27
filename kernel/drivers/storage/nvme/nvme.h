#ifndef OS_KERNEL_DRIVERS_STORAGE_NVME_H
#define OS_KERNEL_DRIVERS_STORAGE_NVME_H

#include <stdint.h>
#include "kernel/storage/block.h"

// ====== nvme_contract ======

// NVM Express Base Specification Rev. 2.0c §3.1: Controller MMIO Registers (BAR0).
#define NVME_REG_CAP           0x0000U
#define NVME_REG_VS            0x0008U
#define NVME_REG_INTMS         0x000CU
#define NVME_REG_INTMC         0x0010U
#define NVME_REG_CC            0x0014U
#define NVME_REG_CSTS          0x001CU
#define NVME_REG_AQA           0x0024U
#define NVME_REG_ASQ           0x0028U
#define NVME_REG_ACQ           0x0030U
#define NVME_REG_DBS           0x1000U

#define NVME_CC_EN             (1U << 0)
#define NVME_CC_SHN_NORMAL     (1U << 14)
#define NVME_CC_IOSQES_64      (6U << 16)
#define NVME_CC_IOCQES_16      (4U << 20)

#define NVME_CSTS_RDY          (1U << 0)
#define NVME_CSTS_CFS          (1U << 1)
#define NVME_CSTS_SHST_MASK    (3U << 2)
#define NVME_CSTS_SHST_DONE    (2U << 2)

// Admin Opcodes (NVMe Base Spec Rev. 2.0c §5).
#define NVME_ADM_CREATE_IO_SQ  0x01U
#define NVME_ADM_CREATE_IO_CQ  0x05U
#define NVME_ADM_IDENTIFY      0x06U

// NVM I/O Opcodes (NVM Command Set Spec Rev. 1.0c §3.2).
#define NVME_NVM_FLUSH         0x00U
#define NVME_NVM_WRITE         0x01U
#define NVME_NVM_READ          0x02U

// 5 contiguous 4 KiB DMA pages: Admin SQ (0), Admin CQ (1), I/O SQ (2), I/O CQ (3), Bounce (4).
#define NVME_DMA_PAGES         5U
#define NVME_QUEUE_DEPTH       16U

typedef struct {
    void     *Context;
    int      (*Read32)(void *context, uint32_t offset, uint32_t *out);
    int      (*Write32)(void *context, uint32_t offset, uint32_t value);
    int      (*Read64)(void *context, uint32_t offset, uint64_t *out);
    int      (*Write64)(void *context, uint32_t offset, uint64_t value);
    int      (*ClockRead)(void *context, uint32_t *ticks_out);
    void     (*Barrier)(void *context);
    void     (*Wait)(void *context, uint64_t expected_seq);
    uint64_t (*IrqSeq)(void *context);
    uint32_t  ClockMask;
    uint32_t  ClockHz;
    uint32_t  IrqEnabled;
} NVME_TRANSPORT;

typedef struct {
    uintptr_t            Self;
    NVME_TRANSPORT       Transport;
    uint8_t             *DmaVirt;
    uint64_t             DmaPhys;
    uint32_t             DoorbellStride;
    uint32_t             Version;
    uint32_t             Nsid;
    uint32_t             SectorBytes;
    uint64_t             TotalSectors;
    uint16_t             AdminSqTail;
    uint16_t             AdminCqHead;
    uint8_t              AdminCqPhase;
    uint16_t             IoSqTail;
    uint16_t             IoCqHead;
    uint8_t              IoCqPhase;
    uint16_t             NextCid;
    uint32_t             Ready;
    KERNEL_BLOCK_DEVICE  BlockDevice;
} KERNEL_NVME_DRIVER;

int  kernel_nvme_init(
    KERNEL_NVME_DRIVER *drv,
    const NVME_TRANSPORT *transport,
    void *dma_virt,
    uint64_t dma_phys
);
void kernel_nvme_shutdown(KERNEL_NVME_DRIVER *drv);

#endif
