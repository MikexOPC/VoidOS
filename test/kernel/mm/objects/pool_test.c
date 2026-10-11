#include "kernel/mm/objects/pool.h"
static KERNEL_OBJECT_POOL pool, other;
__declspec(align(4096)) static unsigned char storage[65536 + 8192];
static unsigned char *arena = storage + 4096;
static KERNEL_OBJECT_HANDLE handles[64];
static unsigned char occupied[4096];

// ====== fixture senza CRT e guardie byte indipendenti dal mapper ===== //

static void clear(void *object, BOOT_U64 bytes)
{
    BOOT_U64 index;
    for (index = 0; index < bytes; ++index) ((volatile unsigned char *)object)[index] = 0;
}

static int fixture(BOOT_U64 capacity)
{
    BOOT_U64 index;
    clear(&pool, sizeof(pool)); clear(&other, sizeof(other));
    clear(handles, sizeof(handles)); clear(occupied, sizeof(occupied));
    for (index = 0; index < sizeof(storage); ++index) ((volatile unsigned char *)storage)[index] = 0xa5;
    return kernel_object_initialize(&pool, arena, capacity);
}

static int guards(BOOT_U64 capacity)
{
    BOOT_U64 index;
    for (index = 0; index < 4096; ++index) if (storage[index] != 0xa5) return 0;
    for (index = 4096 + capacity; index < sizeof(storage); ++index) if (storage[index] != 0xa5) return 0;
    return 1;
}

// ====== allineamento contenuto azzerato ownership e durata degli handle ===== //

static int ownership(void)
{
    KERNEL_OBJECT_HANDLE first = {0}, second = {0}, stale, changed;
    unsigned char *data;
    BOOT_U64 alignment, index;
    if (!fixture(65536)) return 1;
    for (alignment = 1; alignment <= 4096; alignment *= 2) {
        if (!kernel_object_allocate(&pool, 17, alignment, &first)) return 2;
        data = kernel_object_data(&pool, &first);
        if (!data || ((BOOT_U64)data & (alignment - 1)) || pool.Used != 17) return 3;
        for (index = 0; index < 17; ++index) { if (data[index]) return 4; data[index] = 0x5a; }
        stale = first;
        changed = first; ++changed.Bytes;
        if (kernel_object_data(&pool, &changed) || kernel_object_release(&pool, &changed) || kernel_object_detach(&pool)) return 5;
        if (!kernel_object_release(&pool, &first) || first.Id || first.Owner || first.Bytes || first.Offset) return 6;
        data = 0;
        if (kernel_object_release(&pool, &first) || kernel_object_data(&pool, &stale)) return 7;
        if (!kernel_object_allocate(&pool, 17, alignment, &second) || second.Id == stale.Id ||
            second.Offset != stale.Offset || kernel_object_release(&pool, &stale)) return 8;
        data = kernel_object_data(&pool, &second);
        for (index = 0; index < 17; ++index) if (data[index]) return 9;
        if (!kernel_object_release(&pool, &second)) return 10;
        data = 0;
    }
    if (!guards(65536) || !kernel_object_detach(&pool) || pool.Arena || pool.Ready || kernel_object_detach(&pool)) return 11;
    if (!kernel_object_initialize(&pool, arena, 65536) || !kernel_object_allocate(&pool, 17, 16, &first) ||
        first.Id <= stale.Id || kernel_object_release(&pool, &stale) || !kernel_object_release(&pool, &first)) return 12;
    return 0;
}

// ====== esaurimento byte record frammentazione e input rifiutati ===== //

static int exhaustion(void)
{
    BOOT_U32 index;
    KERNEL_OBJECT_HANDLE extra = {0};
    if (!fixture(4096) || !kernel_object_allocate(&pool, 4096, 4096, &extra)) return 1;
    if (kernel_object_allocate(&pool, 1, 1, &handles[0]) || pool.Used != 4096 || !kernel_object_release(&pool, &extra)) return 2;
    for (index = 0; index < 64; ++index)
        if (!kernel_object_allocate(&pool, 1, 1, &handles[index])) return 3;
    if (kernel_object_allocate(&pool, 1, 1, &extra) || pool.Count != 64) return 4;
    for (index = 0; index < 64; ++index) if (!kernel_object_release(&pool, &handles[index])) return 5;
    for (index = 0; index < 4; ++index)
        if (!kernel_object_allocate(&pool, 1024, 1, &handles[index])) return 6;
    if (!kernel_object_release(&pool, &handles[0]) || !kernel_object_release(&pool, &handles[2])) return 7;
    if (kernel_object_allocate(&pool, 2048, 1, &extra) || pool.Used != 2048 ||
        !kernel_object_release(&pool, &handles[1]) || !kernel_object_release(&pool, &handles[3]) ||
        !kernel_object_allocate(&pool, 4096, 1, &extra) || !kernel_object_release(&pool, &extra)) return 8;
    if (kernel_object_allocate(&pool, 0, 1, &extra) || kernel_object_allocate(&pool, ~0ULL, 16, &extra) ||
        kernel_object_allocate(&pool, 1, 0, &extra) || kernel_object_allocate(&pool, 1, 3, &extra) ||
        kernel_object_allocate(&pool, 1, 8192, &extra) || pool.Count || pool.Used) return 9;
    pool.NextId = ~0ULL;
    if (kernel_object_allocate(&pool, 1, 1, &extra) || extra.Id || !guards(4096) ||
        !kernel_object_detach(&pool) || !kernel_object_initialize(&pool, arena, 4096) ||
        pool.NextId != ~0ULL || kernel_object_allocate(&pool, 1, 1, &extra)) return 10;
    return 0;
}

// ====== owner copiato alias dei metadati e record corrotti ===== //

static int malformed(void)
{
    KERNEL_OBJECT_HANDLE handle = {0}, changed;
    BOOT_U32 scenario, byte;
    if (kernel_object_initialize(0, arena, 4096) || kernel_object_validate(0) ||
        kernel_object_allocate(0, 1, 1, &handle) || kernel_object_data(0, &handle) ||
        kernel_object_release(0, &handle) || kernel_object_detach(0)) return 13;
    if (!fixture(4096) || kernel_object_initialize(&pool, arena, 4096) ||
        kernel_object_allocate(&pool, 1, 1, (KERNEL_OBJECT_HANDLE *)&pool.Records[0]) ||
        kernel_object_allocate(&pool, 1, 1, (KERNEL_OBJECT_HANDLE *)arena)) return 1;
    if (kernel_object_initialize(&other, arena + 1, 4096) || kernel_object_initialize(&other, arena, 0) ||
        kernel_object_initialize(&other, arena, 65537)) return 2;
    if (!kernel_object_allocate(&pool, 32, 16, &handle)) return 3;
    if (kernel_object_allocate(&pool, 1, 1, 0) || kernel_object_data(&pool, 0) ||
        kernel_object_release(&pool, 0) || kernel_object_allocate(&pool, 1, 1, &handle)) return 14;
    for (byte = 0; byte < sizeof(pool); ++byte)
        ((volatile unsigned char *)&other)[byte] = ((const unsigned char *)&pool)[byte];
    if (kernel_object_data(&other, &handle) || kernel_object_release(&other, &handle) || kernel_object_validate(&other)) return 4;
    changed = handle; ++changed.Offset;
    if (kernel_object_data(&pool, &changed) || kernel_object_release(&pool, &changed)) return 5;
    for (scenario = 0; scenario < 5; ++scenario) {
        BOOT_U64 saved;
        switch (scenario) {
        case 0: saved = pool.Records[0].Bytes; pool.Records[0].Bytes = ~0ULL; break;
        case 1: saved = pool.Records[0].Alignment; pool.Records[0].Alignment = 3; break;
        case 2: saved = pool.Records[0].Offset; pool.Records[0].Offset = 4096; break;
        case 3: saved = pool.Records[0].Id; pool.Records[0].Id = pool.NextId; break;
        default: saved = pool.Used; ++pool.Used; break;
        }
        if (kernel_object_validate(&pool) || kernel_object_data(&pool, &handle) || kernel_object_release(&pool, &handle)) return 6;
        switch (scenario) {
        case 0: pool.Records[0].Bytes = saved; break;
        case 1: pool.Records[0].Alignment = saved; break;
        case 2: pool.Records[0].Offset = saved; break;
        case 3: pool.Records[0].Id = saved; break;
        default: pool.Used = saved; break;
        }
    }
    if (!kernel_object_release(&pool, &handle) || !guards(4096)) return 7;
    pool.Records[63].Id = 1;
    if (kernel_object_validate(&pool) || kernel_object_allocate(&pool, 1, 1, &handle)) return 8;
    pool.Records[63].Id = 0;
    if (!kernel_object_allocate(&pool, 32, 16, &handles[0]) ||
        !kernel_object_allocate(&pool, 32, 16, &handles[1])) return 9;
    pool.Records[1].Id = pool.Records[0].Id;
    if (kernel_object_validate(&pool) || kernel_object_release(&pool, &handles[0])) return 10;
    pool.Records[1].Id = handles[1].Id;
    pool.Records[1].Offset = 0;
    if (kernel_object_validate(&pool) || kernel_object_data(&pool, &handles[0])) return 11;
    pool.Records[1].Offset = handles[1].Offset;
    if (!kernel_object_release(&pool, &handles[0]) || !kernel_object_release(&pool, &handles[1])) return 12;
    return 0;
}

// ====== modello bitmap indipendente per 1024 operazioni riproducibili ===== //

static int sequences(void)
{
    BOOT_U32 step, seed = 0x71435, slot, index;
    BOOT_U64 used = 0;
    if (!fixture(4096)) return 1;
    for (step = 0; step < 1024; ++step) {
        seed = seed * 1664525U + 1013904223U;
        slot = seed % 64;
        if (handles[slot].Id) {
            BOOT_U64 base = handles[slot].Offset, bytes = handles[slot].Bytes;
            if (!kernel_object_release(&pool, &handles[slot])) return 2;
            for (index = 0; index < bytes; ++index) {
                occupied[base + index] = 0;
                if (arena[base + index]) return 3;
            }
            used -= bytes;
        } else {
            BOOT_U64 bytes = 1 + ((seed >> 8) % 127), alignment = 1ULL << ((seed >> 16) % 9);
            BOOT_U64 base, end, candidate = ~0ULL;
            unsigned char *data;
            for (base = 0; base + bytes <= 4096; base += alignment) {
                for (end = base; end < base + bytes; ++end) if (occupied[end]) break;
                if (end == base + bytes) { candidate = base; break; }
            }
            if (candidate == ~0ULL) {
                if (kernel_object_allocate(&pool, bytes, alignment, &handles[slot])) return 4;
            } else {
                if (!kernel_object_allocate(&pool, bytes, alignment, &handles[slot]) || handles[slot].Offset != candidate) return 5;
                data = kernel_object_data(&pool, &handles[slot]);
                if (!data) return 6;
                for (index = 0; index < bytes; ++index) {
                    if (data[index]) return 7;
                    data[index] = 0x5a; occupied[candidate + index] = 1;
                }
                used += bytes;
                data = 0;
            }
        }
        if (!kernel_object_validate(&pool) || pool.Used != used || !guards(4096)) return 8;
        for (slot = 0; slot < 64; ++slot) {
            if (handles[slot].Id) {
                unsigned char *data = kernel_object_data(&pool, &handles[slot]);
                if (!data) return 11;
                for (index = 0; index < handles[slot].Bytes; ++index) if (data[index] != 0x5a) return 12;
                data = 0;
            }
        }
    }
    for (slot = 0; slot < 64; ++slot)
        if (handles[slot].Id && !kernel_object_release(&pool, &handles[slot])) return 9;
    return pool.Used || pool.Count || !kernel_object_detach(&pool) ? 10 : 0;
}

// ====== aggregazione con identificazione della famiglia fallita ===== //

__declspec(dllexport) int run_object_tests(void)
{
    int result = ownership();
    if (result) return 100 + result;
    result = exhaustion();
    if (result) return 200 + result;
    result = malformed();
    if (result) return 300 + result;
    result = sequences();
    return result ? 400 + result : 0;
}
