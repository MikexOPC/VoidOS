#include "kernel/mm/physical/allocator.h"
static KERNEL_PHYSICAL_ALLOCATOR owner, other_owner;
static unsigned char map[48 * 129];
static KERNEL_PHYSICAL_LEASE leases[64];

// ====== fixture locale e reset volatile senza dipendenze da CRT ====== //

static void clear(void *object, BOOT_U64 bytes)
{
    volatile unsigned char *data = (volatile unsigned char *)object;
    BOOT_U64 index;
    for (index = 0; index < bytes; ++index) data[index] = 0;
}

static void put(BOOT_U32 offset, BOOT_U64 value, BOOT_U32 width)
{
    BOOT_U32 index;
    for (index = 0; index < width; ++index) { map[offset + index] = (unsigned char)value; value >>= 8; }
}

static void region(BOOT_U32 index, BOOT_U32 type, BOOT_U64 base, BOOT_U64 pages, BOOT_U64 attributes)
{
    BOOT_U32 offset = index * 48;
    put(offset, type, 4); put(offset + 8, base, 8); put(offset + 24, pages, 8); put(offset + 32, attributes, 8);
}

static int initialize(void)
{
    clear(&owner, sizeof(owner));
    region(0, 7, 0x9000, 2, 15);
    region(1, 2, 0x1000, 2, 15);
    region(2, 7, 0x5000, 4, 15);
    region(3, 7, 0x3000, 2, 0);
    return kernel_physical_initialize(&owner, map, 192, 48, 32);
}

// ====== contiguita coalescenza handle alterati doppi rilasci e ABA ====== //

static int ownership(void)
{
    KERNEL_PHYSICAL_LEASE first = {0}, second = {0}, stale, changed;
    if (!initialize() || owner.TotalPages != 6 || owner.Count != 1 || !kernel_physical_validate(&owner)) return 1;
    if (!kernel_physical_allocate(&owner, 2, &first) || first.Base != 0x5000 || owner.FreePages != 4) return 2;
    stale = first;
    changed = first; changed.Pages = 1;
    if (kernel_physical_release(&owner, &changed) || owner.FreePages != 4) return 3;
    if (!kernel_physical_release(&owner, &first) || kernel_physical_release(&owner, &first) ||
        !kernel_physical_allocate(&owner, 2, &second) || second.Base != stale.Base || second.Id == stale.Id ||
        kernel_physical_release(&owner, &stale) || !kernel_physical_release(&owner, &second)) return 4;
    clear(&other_owner, sizeof(other_owner));
    if (!kernel_physical_initialize(&other_owner, map, 192, 48, 32) || !kernel_physical_allocate(&owner, 1, &first) ||
        kernel_physical_release(&other_owner, &first) || !kernel_physical_release(&owner, &first)) return 5;
    if (kernel_physical_initialize(&owner, map, 192, 48, 32) ||
        kernel_physical_allocate(&owner, 1, &owner.Live[0]) ||
        kernel_physical_allocate(&owner, 7, &first) || first.Id || owner.FreePages != 6) return 6;
    first.Id = 17;
    if (kernel_physical_allocate(&owner, 1, &first) || first.Id != 17 || owner.FreePages != 6) return 9;
    first.Id = 0;
    owner.NextId = ~0ULL;
    if (kernel_physical_allocate(&owner, 1, &first) || first.Id) return 7;
    owner.NextId = 3;
    owner.Free[0].Pages = 7;
    if (kernel_physical_allocate(&owner, 1, &first) || first.Id) return 8;
    return 0;
}

// ====== esaurimento dei record e ripristino senza perdita di pagine ====== //

static int exhaustion(void)
{
    BOOT_U32 index;
    KERNEL_PHYSICAL_LEASE extra = {0}, empty = {0};
    clear(&owner, sizeof(owner)); clear(leases, sizeof(leases));
    region(0, 7, 0x1000, 128, 8);
    if (!kernel_physical_initialize(&owner, map, 48, 48, 32)) return 1;
    for (index = 0; index < 64; ++index) if (!kernel_physical_allocate(&owner, 1, &leases[index])) return 2;
    if (kernel_physical_allocate(&owner, 1, &extra) || extra.Id || owner.FreePages != 64) return 3;
    for (index = 0; index < 64; index += 2) if (!kernel_physical_release(&owner, &leases[index])) return 4;
    for (index = 1; index < 64; index += 2) if (!kernel_physical_release(&owner, &leases[index])) return 5;
    if (owner.Count != 1 || owner.FreePages != 128 || !kernel_physical_allocate(&owner, 128, &extra) ||
        owner.Count || owner.FreePages || !kernel_physical_validate(&owner) || kernel_physical_allocate(&owner, 1, &empty) ||
        !kernel_physical_release(&owner, &extra) || owner.FreePages != 128) return 6;
    clear(&owner, sizeof(owner));
    region(0, 7, 0x1000, 1, 8); region(1, 7, 0x4000, 1, 8);
    if (!kernel_physical_initialize(&owner, map, 96, 48, 32) || kernel_physical_allocate(&owner, 2, &empty) ||
        owner.FreePages != 2 || owner.Count != 2 || empty.Id) return 7;
    return 0;
}

// ====== mappa invalida limiti CPU e capacita metadati non modificano output ====== //

static int rejected_maps(void)
{
    BOOT_U32 index;
    clear(&owner, sizeof(owner));
    region(0, 7, 0x1000, 2, 8); region(1, 7, 0x2000, 2, 8);
    if (kernel_physical_initialize(&owner, map, 96, 48, 32) || owner.Ready || owner.Count) return 1;
    region(0, 7, 1ULL << 32, 1, 8);
    if (kernel_physical_initialize(&owner, map, 48, 48, 32) || owner.Ready) return 2;
    region(0, 7, 0x1000, 1, 8 | (1ULL << 63));
    if (kernel_physical_initialize(&owner, map, 48, 48, 32) || owner.Ready) return 3;
    for (index = 0; index < 129; ++index) region(index, 7, 0x1000 + index * 8192, 1, 8);
    if (kernel_physical_initialize(&owner, map, sizeof(map), 48, 32) || owner.Ready || owner.Count) return 4;
    if (kernel_physical_initialize(&owner, (const unsigned char *)&owner, sizeof(owner), 48, 32)) return 5;
    return 0;
}

// ====== 1024 sequenze con modello indipendente di pagine possedute ====== //

static int sequences(void)
{
    BOOT_U32 round, slot, seed = 73;
    BOOT_U64 used;
    for (round = 0; round < 1024; ++round) {
        clear(&owner, sizeof(owner)); clear(leases, sizeof(leases));
        region(0, 7, 0x1000, 128, 8);
        if (!kernel_physical_initialize(&owner, map, 48, 48, 32)) return 1;
        used = 0;
        for (slot = 0; slot < 32; ++slot) {
            BOOT_U64 pages;
            seed = seed * 1664525U + 1013904223U;
            pages = 1 + ((seed >> 24) & 3);
            if (!kernel_physical_allocate(&owner, pages, &leases[slot])) return 2;
            if (leases[slot].Base != 0x1000 + used * 4096 || leases[slot].Pages != pages) return 3;
            used += pages;
            if (owner.FreePages != 128 - used) return 4;
        }
        for (slot = 32; slot; --slot) if (!kernel_physical_release(&owner, &leases[slot - 1])) return 5;
        if (owner.FreePages != 128 || owner.Count != 1 || !kernel_physical_validate(&owner)) return 6;
    }
    return 0;
}

__declspec(dllexport) int run_physical_tests(void)
{
    int result = ownership();
    if (result) return 100 + result;
    result = exhaustion(); if (result) return 200 + result;
    result = rejected_maps(); if (result) return 300 + result;
    result = sequences(); return result ? 400 + result : 0;
}
