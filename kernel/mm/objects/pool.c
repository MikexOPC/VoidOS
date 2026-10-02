#include "pool.h"
BOOT_U32 kernel_object_request(BOOT_U64 bytes, BOOT_U64 alignment);
BOOT_U64 kernel_object_position(BOOT_U64 start, BOOT_U64 end, BOOT_U64 bytes, BOOT_U64 alignment);
BOOT_U32 kernel_object_record(BOOT_U64 offset, BOOT_U64 bytes, BOOT_U64 alignment, BOOT_U64 capacity);

// ====== intervalli accessibili distinti da metadati e handle ===== //

static int overlaps(const void *first, BOOT_U64 bytes, const void *second, BOOT_U64 other)
{
    BOOT_U64 a = (BOOT_U64)first, b = (BOOT_U64)second;
    return a > ~0ULL - bytes || b > ~0ULL - other || (a < b + other && b < a + bytes);
}

// ====== record inutilizzati interamente zero ===== //

static int empty_record(const KERNEL_OBJECT_RECORD *record)
{
    return !record->Offset && !record->Bytes && !record->Alignment && !record->Id;
}

// ====== handle conservati fuori dal buffer e dai metadati ===== //

static int handle_external(const KERNEL_OBJECT_POOL *pool, const KERNEL_OBJECT_HANDLE *handle)
{
    return handle && !overlaps(pool, sizeof(*pool), handle, sizeof(*handle)) &&
           !overlaps(pool->Arena, pool->Capacity, handle, sizeof(*handle));
}

// ====== invarianti senza accessi ai byte del buffer preso in prestito ===== //

int kernel_object_validate(const KERNEL_OBJECT_POOL *pool)
{
    BOOT_U32 index, other;
    BOOT_U64 used = 0, previous_end = 0;
    if (!pool || pool->Self != (BOOT_U64)pool || pool->Ready != 1 || !pool->NextId ||
        !pool->Arena || ((BOOT_U64)pool->Arena & 4095) || pool->Capacity < 4096 ||
        pool->Capacity > KERNEL_OBJECT_MAX_BYTES || (pool->Capacity & 4095) ||
        pool->Count > KERNEL_OBJECT_RECORDS || pool->Used > pool->Capacity ||
        overlaps(pool, sizeof(*pool), pool->Arena, pool->Capacity)) {
        return 0;
    }
    for (index = 0; index < pool->Count; ++index) {
        const KERNEL_OBJECT_RECORD *record = &pool->Records[index];
        if (!record->Id || record->Id >= pool->NextId || record->Offset < previous_end ||
            !kernel_object_record(record->Offset, record->Bytes, record->Alignment, pool->Capacity) ||
            record->Bytes > pool->Capacity - used) {
            return 0;
        }
        previous_end = record->Offset + record->Bytes;
        used += record->Bytes;
        for (other = 0; other < index; ++other) {
            if (pool->Records[other].Id == record->Id) return 0;
        }
    }
    for (; index < KERNEL_OBJECT_RECORDS; ++index) {
        if (!empty_record(&pool->Records[index])) return 0;
    }
    return used == pool->Used;
}

// ====== prima inizializzazione zero oppure riapertura con ID preservati ===== //

int kernel_object_initialize(KERNEL_OBJECT_POOL *pool, void *arena, BOOT_U64 bytes)
{
    BOOT_U64 index;
    int fresh = 1;
    if (!pool || !arena || ((BOOT_U64)arena & 4095) || bytes < 4096 || bytes > KERNEL_OBJECT_MAX_BYTES ||
        (bytes & 4095) || overlaps(pool, sizeof(*pool), arena, bytes)) {
        return 0;
    }
    for (index = 0; index < sizeof(*pool); ++index) {
        if (((const unsigned char *)pool)[index]) { fresh = 0; break; }
    }
    if (!fresh) {
        if (pool->Self != (BOOT_U64)pool || pool->Ready || pool->Arena || pool->Count ||
            pool->Capacity || pool->Used || !pool->NextId) {
            return 0;
        }
        for (index = 0; index < KERNEL_OBJECT_RECORDS; ++index) {
            if (!empty_record(&pool->Records[index])) return 0;
        }
    }
    pool->Self = (BOOT_U64)pool;
    pool->Arena = (unsigned char *)arena;
    pool->Capacity = bytes;
    if (fresh) {
        pool->NextId = 1;
    }
    pool->Ready = 1;
    return 1;
}

// ====== ricerca del primo gap senza cambiare i metadati ===== //

static int find_gap(const KERNEL_OBJECT_POOL *pool, BOOT_U64 bytes, BOOT_U64 alignment,
                    BOOT_U32 *slot, BOOT_U64 *offset)
{
    BOOT_U64 start = 0;
    BOOT_U32 index;
    for (index = 0; index <= pool->Count; ++index) {
        BOOT_U64 end = index < pool->Count ? pool->Records[index].Offset : pool->Capacity;
        BOOT_U64 encoded = kernel_object_position(start, end, bytes, alignment);
        if (encoded) {
            *slot = index;
            *offset = encoded - 1;
            return 1;
        }
        if (index < pool->Count) {
            start = pool->Records[index].Offset + pool->Records[index].Bytes;
        }
    }
    return 0;
}

// ====== azzeramento limitato al record gia validato ===== //

static void zero_object(KERNEL_OBJECT_POOL *pool, BOOT_U64 offset, BOOT_U64 bytes)
{
    BOOT_U64 index;
    volatile unsigned char *data = pool->Arena + offset;
    for (index = 0; index < bytes; ++index) {
        data[index] = 0;
    }
}

// ====== allocazione senza arrotondare i byte richiesti o consumare i gap ===== //

int kernel_object_allocate(KERNEL_OBJECT_POOL *pool, BOOT_U64 bytes, BOOT_U64 alignment, KERNEL_OBJECT_HANDLE *output)
{
    BOOT_U32 slot, index;
    BOOT_U64 offset;
    if (!kernel_object_validate(pool) || !handle_external(pool, output) ||
        !kernel_object_request(bytes, alignment) || output->Offset || output->Bytes || output->Id || output->Owner ||
        pool->Count == KERNEL_OBJECT_RECORDS || pool->NextId == ~0ULL ||
        !find_gap(pool, bytes, alignment, &slot, &offset)) {
        return 0;
    }
    zero_object(pool, offset, bytes);
    for (index = pool->Count; index > slot; --index) {
        pool->Records[index] = pool->Records[index - 1];
    }
    pool->Records[slot] = (KERNEL_OBJECT_RECORD){offset, bytes, alignment, pool->NextId};
    *output = (KERNEL_OBJECT_HANDLE){offset, bytes, pool->NextId, (BOOT_U64)pool};
    ++pool->NextId;
    ++pool->Count;
    pool->Used += bytes;
    return 1;
}

// ====== identita esatta dell oggetto prima di fornire un puntatore ===== //

static BOOT_U32 handle_slot(const KERNEL_OBJECT_POOL *pool, const KERNEL_OBJECT_HANDLE *handle)
{
    BOOT_U32 slot;
    if (!kernel_object_validate(pool) || !handle_external(pool, handle) ||
        !handle->Id || handle->Owner != (BOOT_U64)pool) {
        return KERNEL_OBJECT_RECORDS;
    }
    for (slot = 0; slot < pool->Count; ++slot) {
        const KERNEL_OBJECT_RECORD *record = &pool->Records[slot];
        if (record->Id == handle->Id && record->Offset == handle->Offset && record->Bytes == handle->Bytes) return slot;
    }
    return KERNEL_OBJECT_RECORDS;
}

// ====== prestito valido solo fino al rilascio del relativo handle ===== //

unsigned char *kernel_object_data(const KERNEL_OBJECT_POOL *pool, const KERNEL_OBJECT_HANDLE *handle)
{
    BOOT_U32 slot = handle_slot(pool, handle);
    return slot == KERNEL_OBJECT_RECORDS ? 0 : pool->Arena + pool->Records[slot].Offset;
}

// ====== cancellazione dei byte prima di rendere riutilizzabile il gap ===== //

int kernel_object_release(KERNEL_OBJECT_POOL *pool, KERNEL_OBJECT_HANDLE *handle)
{
    BOOT_U32 index, slot = handle_slot(pool, handle);
    if (slot == KERNEL_OBJECT_RECORDS) {
        return 0;
    }
    zero_object(pool, pool->Records[slot].Offset, pool->Records[slot].Bytes);
    pool->Used -= pool->Records[slot].Bytes;
    for (index = slot + 1; index < pool->Count; ++index) {
        pool->Records[index - 1] = pool->Records[index];
    }
    --pool->Count;
    pool->Records[pool->Count] = (KERNEL_OBJECT_RECORD){0};
    *handle = (KERNEL_OBJECT_HANDLE){0};
    return 1;
}

// ====== scollegamento consentito solo senza oggetti vivi ===== //

int kernel_object_detach(KERNEL_OBJECT_POOL *pool)
{
    if (!kernel_object_validate(pool) || pool->Count || pool->Used) {
        return 0;
    }
    pool->Arena = 0;
    pool->Capacity = 0;
    pool->Ready = 0;
    return 1;
}
