#include "kernel/storage/cache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Host test of KERNEL_BLOCK_CACHE logic around a RAM-backed mock KERNEL_BLOCK_DEVICE
// (AGENTS.md Method B: the mock tests the cache logic and error recovery, not hardware I/O).

#define TEST_SECTORS       256U
#define TEST_SECTOR_BYTES  512U

static __declspec(align(4096)) uint8_t heap_arena[KERNEL_HEAP_MAX_PAGES * 4096ULL];
static KERNEL_HEAP test_heap;

typedef struct {
    uint8_t   Storage[TEST_SECTORS * TEST_SECTOR_BYTES];
    uint32_t  ReadCalls;
    uint32_t  WriteCalls;
    uint32_t  FlushCalls;
    int       FailNextRead;
    int       FailNextWrite;
    int       FailNextFlush;
} MOCK_DISK;

static MOCK_DISK mock_disk;
static KERNEL_BLOCK_DEVICE mock_device;

#define CHECK(cond) do { \
    if (!(cond)) { \
        printf("FAIL block_cache_host line %d\n", __LINE__); \
        exit(1); \
    } \
} while (0)

// ====== mock_read ======

static int mock_read(KERNEL_BLOCK_DEVICE *device, uint64_t lba, uint32_t count, void *buffer)
{
    MOCK_DISK *disk = (MOCK_DISK *)device->Context;
    ++disk->ReadCalls;
    if (disk->FailNextRead) {
        disk->FailNextRead = 0;
        return KERNEL_STORAGE_ERR_IO;
    }
    memcpy(buffer, disk->Storage + lba * TEST_SECTOR_BYTES, (size_t)count * TEST_SECTOR_BYTES);
    return KERNEL_STORAGE_OK;
}

// ====== mock_write ======

static int mock_write(KERNEL_BLOCK_DEVICE *device, uint64_t lba, uint32_t count, const void *buffer)
{
    MOCK_DISK *disk = (MOCK_DISK *)device->Context;
    ++disk->WriteCalls;
    if (disk->FailNextWrite) {
        disk->FailNextWrite = 0;
        return KERNEL_STORAGE_ERR_IO;
    }
    memcpy(disk->Storage + lba * TEST_SECTOR_BYTES, buffer, (size_t)count * TEST_SECTOR_BYTES);
    return KERNEL_STORAGE_OK;
}

// ====== mock_flush ======

static int mock_flush(KERNEL_BLOCK_DEVICE *device)
{
    MOCK_DISK *disk = (MOCK_DISK *)device->Context;
    ++disk->FlushCalls;
    if (disk->FailNextFlush) {
        disk->FailNextFlush = 0;
        return KERNEL_STORAGE_ERR_TIMEOUT;
    }
    return KERNEL_STORAGE_OK;
}

// ====== setup_fixture ======

static void setup_fixture(void)
{
    memset(&test_heap, 0, sizeof(test_heap));
    CHECK(kernel_heap_init(&test_heap, heap_arena, KERNEL_HEAP_MAX_PAGES));

    memset(&mock_disk, 0, sizeof(mock_disk));
    for (uint32_t sector = 0; sector < TEST_SECTORS; ++sector) {
        for (uint32_t byte = 0; byte < TEST_SECTOR_BYTES; ++byte)
            mock_disk.Storage[sector * TEST_SECTOR_BYTES + byte] = (uint8_t)((sector + byte) & 0xffU);
    }

    memset(&mock_device, 0, sizeof(mock_device));
    mock_device.Self = (uintptr_t)&mock_device;
    mock_device.Name = "mock0";
    mock_device.Context = &mock_disk;
    mock_device.TotalSectors = TEST_SECTORS;
    mock_device.SectorBytes = TEST_SECTOR_BYTES;
    mock_device.MaxTransferSectors = 4U;
    mock_device.Kind = KERNEL_BLOCK_KIND_MEMORY;
    mock_device.ReadOnly = 0U;
    mock_device.Ready = 1U;
    mock_device.Read = mock_read;
    mock_device.Write = mock_write;
    mock_device.Flush = mock_flush;
    CHECK(kernel_block_device_valid(&mock_device));
}

// ====== test_lru_and_hits ======

static void test_lru_and_hits(void)
{
    KERNEL_BLOCK_CACHE cache;
    CHECK(kernel_block_cache_init(&cache, &test_heap, &mock_device, 4U) == KERNEL_STORAGE_OK);
    CHECK(kernel_block_cache_valid(&cache));

    uint8_t buf[TEST_SECTOR_BYTES];
    // Read block 0 (LBA 0), block 1 (LBA 8), block 2 (LBA 16), block 3 (LBA 24): 4 misses.
    for (uint32_t block = 0; block < 4U; ++block) {
        CHECK(kernel_block_cache_read_sectors(&cache, block * 8ULL, 1U, buf) == KERNEL_STORAGE_OK);
        CHECK(buf[0] == (uint8_t)((block * 8U) & 0xffU));
    }

    KERNEL_BLOCK_CACHE_STATS stats = kernel_block_cache_stats(&cache);
    CHECK(stats.Misses == 4U && stats.Hits == 0U && stats.ValidLines == 4U && stats.Evictions == 0U);
    // MaxTransferSectors is 4, and 1 block = 8 sectors -> 2 ReadCalls per block = 8 ReadCalls.
    CHECK(mock_disk.ReadCalls == 8U);

    // Touch block 0 again -> hit, no new device read, block 1 becomes LRU oldest.
    CHECK(kernel_block_cache_read_sectors(&cache, 1ULL, 1U, buf) == KERNEL_STORAGE_OK);
    stats = kernel_block_cache_stats(&cache);
    CHECK(stats.Hits == 1U && mock_disk.ReadCalls == 8U);

    // Load block 4 (LBA 32) -> evicts block 1 (LRU oldest), keeps block 0 in cache.
    CHECK(kernel_block_cache_read_sectors(&cache, 32ULL, 1U, buf) == KERNEL_STORAGE_OK);
    stats = kernel_block_cache_stats(&cache);
    CHECK(stats.Misses == 5U && stats.Evictions == 1U && stats.ValidLines == 4U);

    // Block 0 must still be a cache hit; block 1 must be a cache miss.
    uint32_t reads_before = mock_disk.ReadCalls;
    CHECK(kernel_block_cache_read_sectors(&cache, 0ULL, 1U, buf) == KERNEL_STORAGE_OK);
    CHECK(mock_disk.ReadCalls == reads_before);
    CHECK(kernel_block_cache_read_sectors(&cache, 8ULL, 1U, buf) == KERNEL_STORAGE_OK);
    CHECK(mock_disk.ReadCalls == reads_before + 2U);

    kernel_block_cache_destroy(&cache);
}

// ====== test_writethrough_and_error_invalidation ======

static void test_writethrough_and_error_invalidation(void)
{
    KERNEL_BLOCK_CACHE cache;
    CHECK(kernel_block_cache_init(&cache, &test_heap, &mock_device, 8U) == KERNEL_STORAGE_OK);

    uint8_t sector_in[TEST_SECTOR_BYTES];
    uint8_t sector_out[TEST_SECTOR_BYTES];
    memset(sector_in, 0xA5, sizeof(sector_in));

    // Populate block 0 in cache.
    CHECK(kernel_block_cache_read_sectors(&cache, 0ULL, 1U, sector_out) == KERNEL_STORAGE_OK);

    // Write-through to LBA 2: must update backing disk immediately AND update cached block 0.
    uint32_t writes_before = mock_disk.WriteCalls;
    CHECK(kernel_block_cache_write_sectors(&cache, 2ULL, 1U, sector_in) == KERNEL_STORAGE_OK);
    CHECK(mock_disk.WriteCalls == writes_before + 1U);
    CHECK(mock_disk.Storage[2U * TEST_SECTOR_BYTES] == 0xA5U);

    // Reading LBA 2 back must hit cache and return 0xA5 without new disk reads.
    uint32_t reads_before = mock_disk.ReadCalls;
    memset(sector_out, 0, sizeof(sector_out));
    CHECK(kernel_block_cache_read_sectors(&cache, 2ULL, 1U, sector_out) == KERNEL_STORAGE_OK);
    CHECK(mock_disk.ReadCalls == reads_before && sector_out[0] == 0xA5U && sector_out[511] == 0xA5U);

    // Byte-granularity write across a 4 KiB block boundary (offset 4090, 16 bytes).
    uint8_t span_in[16];
    uint8_t span_out[16];
    for (uint32_t i = 0; i < 16U; ++i)
        span_in[i] = (uint8_t)(0x30U + i);
    CHECK(kernel_block_cache_write_bytes(&cache, 4090ULL, 16ULL, span_in) == KERNEL_STORAGE_OK);
    CHECK(kernel_block_cache_read_bytes(&cache, 4090ULL, 16ULL, span_out) == KERNEL_STORAGE_OK);
    CHECK(memcmp(span_in, span_out, 16U) == 0);
    CHECK(memcmp(mock_disk.Storage + 4090U, span_in, 16U) == 0);

    // Simulated I/O failure on write: must invalidate cached block 0 so stale data is never served.
    mock_disk.FailNextWrite = 1;
    memset(sector_in, 0xEE, sizeof(sector_in));
    CHECK(kernel_block_cache_write_sectors(&cache, 2ULL, 1U, sector_in) == KERNEL_STORAGE_ERR_IO);
    reads_before = mock_disk.ReadCalls;
    CHECK(kernel_block_cache_read_sectors(&cache, 2ULL, 1U, sector_out) == KERNEL_STORAGE_OK);
    CHECK(mock_disk.ReadCalls > reads_before);
    CHECK(sector_out[0] == 0xA5U);

    // Flush and bounds checks.
    CHECK(kernel_block_cache_flush(&cache) == KERNEL_STORAGE_OK && mock_disk.FlushCalls == 1U);
    mock_disk.FailNextFlush = 1;
    CHECK(kernel_block_cache_flush(&cache) == KERNEL_STORAGE_ERR_TIMEOUT);

    CHECK(kernel_block_cache_read_sectors(&cache, TEST_SECTORS, 1U, sector_out) == KERNEL_STORAGE_ERR_RANGE);
    CHECK(kernel_block_cache_read_sectors(&cache, TEST_SECTORS - 2U, 4U, sector_out) == KERNEL_STORAGE_ERR_RANGE);
    CHECK(kernel_block_cache_read_bytes(&cache, (uint64_t)TEST_SECTORS * TEST_SECTOR_BYTES - 4ULL, 8ULL, span_out) == KERNEL_STORAGE_ERR_RANGE);

    mock_device.ReadOnly = 1U;
    CHECK(kernel_block_cache_write_sectors(&cache, 0ULL, 1U, sector_in) == KERNEL_STORAGE_ERR_READ_ONLY);
    CHECK(kernel_block_cache_write_bytes(&cache, 0ULL, 4ULL, span_in) == KERNEL_STORAGE_ERR_READ_ONLY);
    mock_device.ReadOnly = 0U;

    kernel_block_cache_destroy(&cache);
}

// ====== main ======

int main(void)
{
    setup_fixture();
    test_lru_and_hits();
    test_writethrough_and_error_invalidation();

    KERNEL_HEAP_STATS heap_stats = kernel_heap_stats(&test_heap);
    CHECK(heap_stats.FreePages == KERNEL_HEAP_MAX_PAGES);
    CHECK(heap_stats.Objects == 0 && heap_stats.RunPages == 0 && heap_stats.SlabPages == 0);

    printf("PASS: block_cache_host (LRU, write-through, cross-block bytes, I/O error invalidation, zero heap leaks)\n");
    return 0;
}
