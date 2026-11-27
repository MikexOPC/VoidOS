#ifndef OS_KERNEL_STORAGE_BLOCK_H
#define OS_KERNEL_STORAGE_BLOCK_H

#include <stddef.h>
#include <stdint.h>

// ====== block_device_contract ======

// Lock order: VFS (120) -> BLOCK_CACHE (130) -> BLOCK_DEVICE (140) -> PROCESS_TABLE (150) -> HEAP (200).
#define KERNEL_VFS_LOCK_RANK           120U
#define KERNEL_BLOCK_CACHE_LOCK_RANK   130U
#define KERNEL_BLOCK_DEVICE_LOCK_RANK  140U

#define KERNEL_STORAGE_OK              0
#define KERNEL_STORAGE_ERR_ARG        (-1)
#define KERNEL_STORAGE_ERR_RANGE      (-2)
#define KERNEL_STORAGE_ERR_READ_ONLY  (-3)
#define KERNEL_STORAGE_ERR_IO         (-4)
#define KERNEL_STORAGE_ERR_TIMEOUT    (-5)
#define KERNEL_STORAGE_ERR_NOMEM      (-6)
#define KERNEL_STORAGE_ERR_CORRUPT    (-7)
#define KERNEL_STORAGE_ERR_NOT_FOUND  (-8)
#define KERNEL_STORAGE_ERR_EXISTS     (-9)
#define KERNEL_STORAGE_ERR_NOSPC      (-10)

#define KERNEL_BLOCK_KIND_NONE         0U
#define KERNEL_BLOCK_KIND_VIRTIO_BLK   1U
#define KERNEL_BLOCK_KIND_AHCI_SATA    2U
#define KERNEL_BLOCK_KIND_MEMORY       3U
#define KERNEL_BLOCK_KIND_USB_MSC      4U
#define KERNEL_BLOCK_KIND_NVME         5U

struct KERNEL_BLOCK_DEVICE;

typedef int (*KERNEL_BLOCK_READ_FN)(
    struct KERNEL_BLOCK_DEVICE *device,
    uint64_t lba,
    uint32_t sector_count,
    void *buffer
);

typedef int (*KERNEL_BLOCK_WRITE_FN)(
    struct KERNEL_BLOCK_DEVICE *device,
    uint64_t lba,
    uint32_t sector_count,
    const void *buffer
);

typedef int (*KERNEL_BLOCK_FLUSH_FN)(
    struct KERNEL_BLOCK_DEVICE *device
);

typedef struct KERNEL_BLOCK_DEVICE {
    uintptr_t              Self;
    const char            *Name;
    void                  *Context;
    uint64_t               TotalSectors;
    uint32_t               SectorBytes;
    uint32_t               MaxTransferSectors;
    uint32_t               Kind;
    uint32_t               ReadOnly;
    uint32_t               Ready;
    uint32_t               Reserved;
    KERNEL_BLOCK_READ_FN   Read;
    KERNEL_BLOCK_WRITE_FN  Write;
    KERNEL_BLOCK_FLUSH_FN  Flush;
} KERNEL_BLOCK_DEVICE;

int kernel_block_device_valid(const KERNEL_BLOCK_DEVICE *device);
int kernel_block_read(KERNEL_BLOCK_DEVICE *device, uint64_t lba, uint32_t sector_count, void *buffer);
int kernel_block_write(KERNEL_BLOCK_DEVICE *device, uint64_t lba, uint32_t sector_count, const void *buffer);
int kernel_block_flush(KERNEL_BLOCK_DEVICE *device);

#endif
