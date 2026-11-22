#ifndef OS_KERNEL_STORAGE_CACHE_H
#define OS_KERNEL_STORAGE_CACHE_H

#include <stddef.h>
#include <stdint.h>
#include "kernel/mm/heap/heap.h"
#include "kernel/storage/block.h"
#include "kernel/sync/lock.h"

// ====== block_cache_contract ======

// Each cache line covers one 4 KiB block (1..8 device sectors of 512..4096 bytes).
// Metadata and data buffers are dynamically allocated on kernel_heap().
#define KERNEL_CACHE_BLOCK_BYTES    4096U
#define KERNEL_CACHE_DEFAULT_LINES  256U
#define KERNEL_CACHE_MAX_LINES      1024U

typedef struct {
    uint64_t  BlockIndex;
    uint64_t  LastAccess;
    uint32_t  Valid;
    uint32_t  Reserved;
} KERNEL_CACHE_LINE;

typedef struct {
    uint64_t  Hits;
    uint64_t  Misses;
    uint64_t  Evictions;
    uint64_t  Reads;
    uint64_t  Writes;
    uint64_t  Flushes;
    uint64_t  Errors;
    uint32_t  ValidLines;
    uint32_t  CapacityLines;
} KERNEL_BLOCK_CACHE_STATS;

typedef struct {
    uintptr_t                 Self;
    KERNEL_HEAP              *Heap;
    KERNEL_BLOCK_DEVICE      *Device;
    KERNEL_CACHE_LINE        *Lines;
    uint8_t                  *Data;
    uint64_t                  TotalBlocks;
    uint64_t                  TailSectors;
    uint64_t                  AccessStamp;
    uint32_t                  SectorsPerBlock;
    uint32_t                  CapacityLines;
    uint32_t                  Ready;
    uint32_t                  IoBusy;
    KERNEL_BLOCK_CACHE_STATS  Stats;
    KERNEL_LOCK               Lock;
} KERNEL_BLOCK_CACHE;

int kernel_block_cache_init(
    KERNEL_BLOCK_CACHE *cache,
    KERNEL_HEAP *heap,
    KERNEL_BLOCK_DEVICE *device,
    uint32_t capacity_lines
);

void kernel_block_cache_destroy(KERNEL_BLOCK_CACHE *cache);
int  kernel_block_cache_valid(const KERNEL_BLOCK_CACHE *cache);

int kernel_block_cache_read_sectors(
    KERNEL_BLOCK_CACHE *cache,
    uint64_t lba,
    uint32_t sector_count,
    void *buffer
);

int kernel_block_cache_write_sectors(
    KERNEL_BLOCK_CACHE *cache,
    uint64_t lba,
    uint32_t sector_count,
    const void *buffer
);

int kernel_block_cache_read_bytes(
    KERNEL_BLOCK_CACHE *cache,
    uint64_t byte_offset,
    uint64_t byte_count,
    void *buffer
);

int kernel_block_cache_write_bytes(
    KERNEL_BLOCK_CACHE *cache,
    uint64_t byte_offset,
    uint64_t byte_count,
    const void *buffer
);

int kernel_block_cache_flush(KERNEL_BLOCK_CACHE *cache);
int kernel_block_cache_invalidate(KERNEL_BLOCK_CACHE *cache);
KERNEL_BLOCK_CACHE_STATS kernel_block_cache_stats(KERNEL_BLOCK_CACHE *cache);

#endif
