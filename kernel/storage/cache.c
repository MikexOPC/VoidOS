#include "cache.h"
#include "kernel/sync/internal.h"

typedef struct {
    KERNEL_BLOCK_CACHE  *Cache;
} CACHE_ACCESS;

// ====== zero_bytes ======

static void zero_bytes(void *pointer, size_t bytes)
{
    uint8_t *target = (uint8_t *)pointer;
    for (size_t index = 0; index < bytes; ++index)
        target[index] = 0;
}

// ====== copy_bytes ======

static void copy_bytes(void *target_pointer, const void *source_pointer, size_t bytes)
{
    uint8_t *target = (uint8_t *)target_pointer;
    const uint8_t *source = (const uint8_t *)source_pointer;
    for (size_t index = 0; index < bytes; ++index)
        target[index] = source[index];
}

// ====== kernel_block_device_valid ======

int kernel_block_device_valid(const KERNEL_BLOCK_DEVICE *device)
{
    if (!device || device->Self != (uintptr_t)device || !device->Ready || !device->Read)
        return 0;
    if (device->SectorBytes != 512U && device->SectorBytes != 4096U)
        return 0;
    if (!device->TotalSectors || !device->MaxTransferSectors)
        return 0;
    if (!device->ReadOnly && !device->Write)
        return 0;
    return 1;
}

// ====== range_valid_private ======

static int range_valid_private(const KERNEL_BLOCK_DEVICE *device, uint64_t lba, uint32_t sector_count)
{
    if (!sector_count || lba >= device->TotalSectors)
        return 0;
    if ((uint64_t)sector_count > device->TotalSectors - lba)
        return 0;
    return 1;
}

// ====== kernel_block_read ======

int kernel_block_read(KERNEL_BLOCK_DEVICE *device, uint64_t lba, uint32_t sector_count, void *buffer)
{
    if (!kernel_block_device_valid(device) || !buffer)
        return KERNEL_STORAGE_ERR_ARG;
    if (!range_valid_private(device, lba, sector_count))
        return KERNEL_STORAGE_ERR_RANGE;

    uint8_t *target = (uint8_t *)buffer;
    uint32_t remaining = sector_count;
    uint64_t current_lba = lba;

    while (remaining) {
        uint32_t chunk = remaining > device->MaxTransferSectors ? device->MaxTransferSectors : remaining;
        int status = device->Read(device, current_lba, chunk, target);
        if (status != KERNEL_STORAGE_OK)
            return status;
        target += (size_t)chunk * device->SectorBytes;
        current_lba += chunk;
        remaining -= chunk;
    }
    return KERNEL_STORAGE_OK;
}

// ====== kernel_block_write ======

int kernel_block_write(KERNEL_BLOCK_DEVICE *device, uint64_t lba, uint32_t sector_count, const void *buffer)
{
    if (!kernel_block_device_valid(device) || !buffer)
        return KERNEL_STORAGE_ERR_ARG;
    if (device->ReadOnly)
        return KERNEL_STORAGE_ERR_READ_ONLY;
    if (!range_valid_private(device, lba, sector_count))
        return KERNEL_STORAGE_ERR_RANGE;

    const uint8_t *source = (const uint8_t *)buffer;
    uint32_t remaining = sector_count;
    uint64_t current_lba = lba;

    while (remaining) {
        uint32_t chunk = remaining > device->MaxTransferSectors ? device->MaxTransferSectors : remaining;
        int status = device->Write(device, current_lba, chunk, source);
        if (status != KERNEL_STORAGE_OK)
            return status;
        source += (size_t)chunk * device->SectorBytes;
        current_lba += chunk;
        remaining -= chunk;
    }
    return KERNEL_STORAGE_OK;
}

// ====== kernel_block_flush ======

int kernel_block_flush(KERNEL_BLOCK_DEVICE *device)
{
    if (!kernel_block_device_valid(device))
        return KERNEL_STORAGE_ERR_ARG;
    if (!device->Flush)
        return KERNEL_STORAGE_OK;
    return device->Flush(device);
}

// ====== enter_private ======

static int enter_private(KERNEL_BLOCK_CACHE *cache, CACHE_ACCESS *access)
{
    if (!cache || !access || cache->Self != (uintptr_t)cache || !cache->Ready)
        return 0;
    for (uint32_t attempt = 0; attempt < 1000000U; ++attempt) {
        uint64_t saved_if = ks_irq_save();
        KERNEL_LOCK_CONTEXT *context = kernel_lock_current_context();
        if (!context) {
            ks_irq_restore(saved_if);
            return 0;
        }
        KERNEL_LOCK_GUARD guard;
        zero_bytes(&guard, sizeof(guard));
        if (!kernel_lock_acquire(&cache->Lock, context, &guard)) {
            ks_irq_restore(saved_if);
            return 0;
        }
        if (cache->Self != (uintptr_t)cache || !cache->Ready) {
            (void)kernel_lock_release(context, &guard);
            ks_irq_restore(saved_if);
            return 0;
        }
        if (!cache->IoBusy) {
            cache->IoBusy = 1U;
            if (!kernel_lock_release(context, &guard))
                ks_fail_stop("BLOCK_CACHE_LOCK_RELEASE_FAILED");
            ks_irq_restore(saved_if);
            access->Cache = cache;
            return 1;
        }
        if (!kernel_lock_release(context, &guard))
            ks_fail_stop("BLOCK_CACHE_LOCK_RELEASE_FAILED");
        ks_irq_restore(saved_if);
        ks_pause();
    }
    return 0;
}

// ====== leave_private ======

static void leave_private(CACHE_ACCESS *access)
{
    KERNEL_BLOCK_CACHE *cache = access ? access->Cache : NULL;
    if (!cache)
        return;
    uint64_t saved_if = ks_irq_save();
    KERNEL_LOCK_CONTEXT *context = kernel_lock_current_context();
    if (!context)
        ks_fail_stop("BLOCK_CACHE_LOCK_CONTEXT_MISSING");
    KERNEL_LOCK_GUARD guard;
    zero_bytes(&guard, sizeof(guard));
    if (!kernel_lock_acquire(&cache->Lock, context, &guard))
        ks_fail_stop("BLOCK_CACHE_LOCK_REACQUIRE_FAILED");
    cache->IoBusy = 0U;
    if (!kernel_lock_release(context, &guard))
        ks_fail_stop("BLOCK_CACHE_LOCK_RELEASE_FAILED");
    ks_irq_restore(saved_if);
    access->Cache = NULL;
}

// ====== kernel_block_cache_init ======

int kernel_block_cache_init(
    KERNEL_BLOCK_CACHE *cache,
    KERNEL_HEAP *heap,
    KERNEL_BLOCK_DEVICE *device,
    uint32_t capacity_lines
)
{
    if (!cache || !heap || !kernel_block_device_valid(device))
        return KERNEL_STORAGE_ERR_ARG;
    if (!capacity_lines)
        capacity_lines = KERNEL_CACHE_DEFAULT_LINES;
    if (capacity_lines > KERNEL_CACHE_MAX_LINES)
        return KERNEL_STORAGE_ERR_ARG;

    zero_bytes(cache, sizeof(*cache));
    uint32_t spb = KERNEL_CACHE_BLOCK_BYTES / device->SectorBytes;
    if (!spb || device->TotalSectors == 0)
        return KERNEL_STORAGE_ERR_ARG;

    KERNEL_CACHE_LINE *lines = (KERNEL_CACHE_LINE *)kernel_heap_alloc(
        heap,
        (uint64_t)capacity_lines * sizeof(KERNEL_CACHE_LINE),
        16
    );
    if (!lines)
        return KERNEL_STORAGE_ERR_NOMEM;

    uint8_t *data = (uint8_t *)kernel_heap_pages(heap, capacity_lines);
    if (!data) {
        (void)kernel_heap_free(heap, lines);
        return KERNEL_STORAGE_ERR_NOMEM;
    }

    if (!kernel_lock_initialize(&cache->Lock, KERNEL_BLOCK_CACHE_LOCK_RANK)) {
        (void)kernel_heap_free(heap, data);
        (void)kernel_heap_free(heap, lines);
        return KERNEL_STORAGE_ERR_ARG;
    }

    cache->Heap = heap;
    cache->Device = device;
    cache->Lines = lines;
    cache->Data = data;
    cache->SectorsPerBlock = spb;
    cache->TotalBlocks = (device->TotalSectors + spb - 1ULL) / spb;
    cache->TailSectors = device->TotalSectors % spb;
    cache->CapacityLines = capacity_lines;
    cache->Stats.CapacityLines = capacity_lines;
    cache->Ready = 1U;
    cache->Self = (uintptr_t)cache;
    return KERNEL_STORAGE_OK;
}

// ====== kernel_block_cache_destroy ======

void kernel_block_cache_destroy(KERNEL_BLOCK_CACHE *cache)
{
    if (!cache || cache->Self != (uintptr_t)cache || !cache->Ready)
        return;
    KERNEL_HEAP *heap = cache->Heap;
    KERNEL_CACHE_LINE *lines = cache->Lines;
    uint8_t *data = cache->Data;
    zero_bytes(cache, sizeof(*cache));
    if (heap && data)
        (void)kernel_heap_free(heap, data);
    if (heap && lines)
        (void)kernel_heap_free(heap, lines);
}

// ====== kernel_block_cache_valid ======

int kernel_block_cache_valid(const KERNEL_BLOCK_CACHE *cache)
{
    return cache && cache->Self == (uintptr_t)cache && cache->Ready == 1U &&
        cache->Heap && kernel_block_device_valid(cache->Device) &&
        cache->Lines && cache->Data && cache->CapacityLines > 0 &&
        cache->CapacityLines <= KERNEL_CACHE_MAX_LINES;
}

// ====== block_sectors_private ======

static uint32_t block_sectors_private(const KERNEL_BLOCK_CACHE *cache, uint64_t block_index)
{
    if (block_index + 1ULL == cache->TotalBlocks && cache->TailSectors)
        return (uint32_t)cache->TailSectors;
    return cache->SectorsPerBlock;
}

// ====== find_line_private ======

static int find_line_private(const KERNEL_BLOCK_CACHE *cache, uint64_t block_index, uint32_t *slot_out)
{
    for (uint32_t index = 0; index < cache->CapacityLines; ++index) {
        if (cache->Lines[index].Valid && cache->Lines[index].BlockIndex == block_index) {
            *slot_out = index;
            return 1;
        }
    }
    return 0;
}

// ====== select_victim_private ======

static uint32_t select_victim_private(KERNEL_BLOCK_CACHE *cache)
{
    uint32_t victim = 0;
    uint64_t oldest = UINT64_MAX;

    for (uint32_t index = 0; index < cache->CapacityLines; ++index) {
        if (!cache->Lines[index].Valid)
            return index;
        if (cache->Lines[index].LastAccess < oldest) {
            oldest = cache->Lines[index].LastAccess;
            victim = index;
        }
    }
    ++cache->Stats.Evictions;
    --cache->Stats.ValidLines;
    cache->Lines[victim].Valid = 0;
    return victim;
}

// ====== fetch_block_private ======

static int fetch_block_private(KERNEL_BLOCK_CACHE *cache, uint64_t block_index, uint32_t *slot_out)
{
    if (block_index >= cache->TotalBlocks)
        return KERNEL_STORAGE_ERR_RANGE;

    uint32_t slot = 0;
    if (find_line_private(cache, block_index, &slot)) {
        cache->Lines[slot].LastAccess = ++cache->AccessStamp;
        ++cache->Stats.Hits;
        *slot_out = slot;
        return KERNEL_STORAGE_OK;
    }

    ++cache->Stats.Misses;
    slot = select_victim_private(cache);
    uint8_t *line_data = cache->Data + (size_t)slot * KERNEL_CACHE_BLOCK_BYTES;
    uint64_t lba = block_index * cache->SectorsPerBlock;
    uint32_t count = block_sectors_private(cache, block_index);

    zero_bytes(line_data, KERNEL_CACHE_BLOCK_BYTES);
    int status = kernel_block_read(cache->Device, lba, count, line_data);
    if (status != KERNEL_STORAGE_OK) {
        ++cache->Stats.Errors;
        return status;
    }

    ++cache->Stats.Reads;
    cache->Lines[slot].BlockIndex = block_index;
    cache->Lines[slot].LastAccess = ++cache->AccessStamp;
    cache->Lines[slot].Valid = 1U;
    ++cache->Stats.ValidLines;
    *slot_out = slot;
    return KERNEL_STORAGE_OK;
}

// ====== invalidate_range_private ======

static void invalidate_range_private(KERNEL_BLOCK_CACHE *cache, uint64_t first_lba, uint32_t sector_count)
{
    uint64_t first_block = first_lba / cache->SectorsPerBlock;
    uint64_t last_block = (first_lba + sector_count - 1ULL) / cache->SectorsPerBlock;

    for (uint32_t index = 0; index < cache->CapacityLines; ++index) {
        if (cache->Lines[index].Valid &&
            cache->Lines[index].BlockIndex >= first_block &&
            cache->Lines[index].BlockIndex <= last_block) {
            cache->Lines[index].Valid = 0;
            --cache->Stats.ValidLines;
        }
    }
}

// ====== kernel_block_cache_read_sectors ======

int kernel_block_cache_read_sectors(
    KERNEL_BLOCK_CACHE *cache,
    uint64_t lba,
    uint32_t sector_count,
    void *buffer
)
{
    if (!cache || !buffer || !sector_count)
        return KERNEL_STORAGE_ERR_ARG;

    CACHE_ACCESS access;
    if (!enter_private(cache, &access))
        return KERNEL_STORAGE_ERR_ARG;

    if (!range_valid_private(cache->Device, lba, sector_count)) {
        leave_private(&access);
        return KERNEL_STORAGE_ERR_RANGE;
    }

    uint8_t *target = (uint8_t *)buffer;
    uint64_t current_lba = lba;
    uint32_t remaining = sector_count;
    uint32_t spb = cache->SectorsPerBlock;
    uint32_t sector_bytes = cache->Device->SectorBytes;

    while (remaining) {
        uint64_t block_index = current_lba / spb;
        uint32_t sector_in_block = (uint32_t)(current_lba % spb);
        uint32_t take = spb - sector_in_block;
        if (take > remaining)
            take = remaining;

        uint32_t slot = 0;
        int status = fetch_block_private(cache, block_index, &slot);
        if (status != KERNEL_STORAGE_OK) {
            leave_private(&access);
            return status;
        }

        const uint8_t *line_data = cache->Data + (size_t)slot * KERNEL_CACHE_BLOCK_BYTES;
        copy_bytes(
            target,
            line_data + (size_t)sector_in_block * sector_bytes,
            (size_t)take * sector_bytes
        );

        target += (size_t)take * sector_bytes;
        current_lba += take;
        remaining -= take;
    }

    leave_private(&access);
    return KERNEL_STORAGE_OK;
}

// ====== kernel_block_cache_write_sectors ======

int kernel_block_cache_write_sectors(
    KERNEL_BLOCK_CACHE *cache,
    uint64_t lba,
    uint32_t sector_count,
    const void *buffer
)
{
    if (!cache || !buffer || !sector_count)
        return KERNEL_STORAGE_ERR_ARG;

    CACHE_ACCESS access;
    if (!enter_private(cache, &access))
        return KERNEL_STORAGE_ERR_ARG;

    if (cache->Device->ReadOnly) {
        leave_private(&access);
        return KERNEL_STORAGE_ERR_READ_ONLY;
    }
    if (!range_valid_private(cache->Device, lba, sector_count)) {
        leave_private(&access);
        return KERNEL_STORAGE_ERR_RANGE;
    }

    int status = kernel_block_write(cache->Device, lba, sector_count, buffer);
    if (status != KERNEL_STORAGE_OK) {
        ++cache->Stats.Errors;
        invalidate_range_private(cache, lba, sector_count);
        leave_private(&access);
        return status;
    }

    ++cache->Stats.Writes;
    const uint8_t *source = (const uint8_t *)buffer;
    uint64_t current_lba = lba;
    uint32_t remaining = sector_count;
    uint32_t spb = cache->SectorsPerBlock;
    uint32_t sector_bytes = cache->Device->SectorBytes;

    while (remaining) {
        uint64_t block_index = current_lba / spb;
        uint32_t sector_in_block = (uint32_t)(current_lba % spb);
        uint32_t take = spb - sector_in_block;
        if (take > remaining)
            take = remaining;

        uint32_t slot = 0;
        if (find_line_private(cache, block_index, &slot)) {
            uint8_t *line_data = cache->Data + (size_t)slot * KERNEL_CACHE_BLOCK_BYTES;
            copy_bytes(
                line_data + (size_t)sector_in_block * sector_bytes,
                source,
                (size_t)take * sector_bytes
            );
            cache->Lines[slot].LastAccess = ++cache->AccessStamp;
        }

        source += (size_t)take * sector_bytes;
        current_lba += take;
        remaining -= take;
    }

    leave_private(&access);
    return KERNEL_STORAGE_OK;
}

// ====== kernel_block_cache_read_bytes ======

int kernel_block_cache_read_bytes(
    KERNEL_BLOCK_CACHE *cache,
    uint64_t byte_offset,
    uint64_t byte_count,
    void *buffer
)
{
    if (!cache || !buffer || !byte_count)
        return KERNEL_STORAGE_ERR_ARG;

    CACHE_ACCESS access;
    if (!enter_private(cache, &access))
        return KERNEL_STORAGE_ERR_ARG;

    uint64_t total_bytes = cache->Device->TotalSectors * cache->Device->SectorBytes;
    if (byte_offset >= total_bytes || byte_count > total_bytes - byte_offset) {
        leave_private(&access);
        return KERNEL_STORAGE_ERR_RANGE;
    }

    uint8_t *target = (uint8_t *)buffer;
    uint64_t current = byte_offset;
    uint64_t remaining = byte_count;

    while (remaining) {
        uint64_t block_index = current / KERNEL_CACHE_BLOCK_BYTES;
        uint32_t offset_in_block = (uint32_t)(current % KERNEL_CACHE_BLOCK_BYTES);
        uint64_t take = KERNEL_CACHE_BLOCK_BYTES - offset_in_block;
        if (take > remaining)
            take = remaining;

        uint32_t slot = 0;
        int status = fetch_block_private(cache, block_index, &slot);
        if (status != KERNEL_STORAGE_OK) {
            leave_private(&access);
            return status;
        }

        const uint8_t *line_data = cache->Data + (size_t)slot * KERNEL_CACHE_BLOCK_BYTES;
        copy_bytes(target, line_data + offset_in_block, (size_t)take);

        target += take;
        current += take;
        remaining -= take;
    }

    leave_private(&access);
    return KERNEL_STORAGE_OK;
}

// ====== kernel_block_cache_write_bytes ======

int kernel_block_cache_write_bytes(
    KERNEL_BLOCK_CACHE *cache,
    uint64_t byte_offset,
    uint64_t byte_count,
    const void *buffer
)
{
    if (!cache || !buffer || !byte_count)
        return KERNEL_STORAGE_ERR_ARG;

    CACHE_ACCESS access;
    if (!enter_private(cache, &access))
        return KERNEL_STORAGE_ERR_ARG;

    if (cache->Device->ReadOnly) {
        leave_private(&access);
        return KERNEL_STORAGE_ERR_READ_ONLY;
    }

    uint64_t total_bytes = cache->Device->TotalSectors * cache->Device->SectorBytes;
    if (byte_offset >= total_bytes || byte_count > total_bytes - byte_offset) {
        leave_private(&access);
        return KERNEL_STORAGE_ERR_RANGE;
    }

    const uint8_t *source = (const uint8_t *)buffer;
    uint64_t current = byte_offset;
    uint64_t remaining = byte_count;
    uint32_t spb = cache->SectorsPerBlock;

    while (remaining) {
        uint64_t block_index = current / KERNEL_CACHE_BLOCK_BYTES;
        uint32_t offset_in_block = (uint32_t)(current % KERNEL_CACHE_BLOCK_BYTES);
        uint64_t take = KERNEL_CACHE_BLOCK_BYTES - offset_in_block;
        if (take > remaining)
            take = remaining;

        uint32_t slot = 0;
        int status = fetch_block_private(cache, block_index, &slot);
        if (status != KERNEL_STORAGE_OK) {
            leave_private(&access);
            return status;
        }

        uint8_t *line_data = cache->Data + (size_t)slot * KERNEL_CACHE_BLOCK_BYTES;
        copy_bytes(line_data + offset_in_block, source, (size_t)take);

        uint64_t lba = block_index * spb;
        uint32_t count = block_sectors_private(cache, block_index);
        status = kernel_block_write(cache->Device, lba, count, line_data);
        if (status != KERNEL_STORAGE_OK) {
            ++cache->Stats.Errors;
            cache->Lines[slot].Valid = 0;
            --cache->Stats.ValidLines;
            leave_private(&access);
            return status;
        }

        ++cache->Stats.Writes;
        cache->Lines[slot].LastAccess = ++cache->AccessStamp;
        source += take;
        current += take;
        remaining -= take;
    }

    leave_private(&access);
    return KERNEL_STORAGE_OK;
}

// ====== kernel_block_cache_flush ======

int kernel_block_cache_flush(KERNEL_BLOCK_CACHE *cache)
{
    CACHE_ACCESS access;
    if (!enter_private(cache, &access))
        return KERNEL_STORAGE_ERR_ARG;

    int status = kernel_block_flush(cache->Device);
    if (status == KERNEL_STORAGE_OK)
        ++cache->Stats.Flushes;
    else
        ++cache->Stats.Errors;

    leave_private(&access);
    return status;
}

// ====== kernel_block_cache_invalidate ======

int kernel_block_cache_invalidate(KERNEL_BLOCK_CACHE *cache)
{
    CACHE_ACCESS access;
    if (!enter_private(cache, &access))
        return KERNEL_STORAGE_ERR_ARG;

    for (uint32_t index = 0; index < cache->CapacityLines; ++index)
        cache->Lines[index].Valid = 0;
    cache->Stats.ValidLines = 0;

    leave_private(&access);
    return KERNEL_STORAGE_OK;
}

// ====== kernel_block_cache_stats ======

KERNEL_BLOCK_CACHE_STATS kernel_block_cache_stats(KERNEL_BLOCK_CACHE *cache)
{
    KERNEL_BLOCK_CACHE_STATS stats = {0};
    CACHE_ACCESS access;
    if (!enter_private(cache, &access))
        return stats;
    stats = cache->Stats;
    leave_private(&access);
    return stats;
}
