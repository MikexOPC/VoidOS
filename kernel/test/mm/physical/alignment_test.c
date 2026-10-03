#include "kernel/mm/physical/allocator.h"
static KERNEL_PHYSICAL_ALLOCATOR owner, snapshot;
static KERNEL_PHYSICAL_LEASE leases[64];
static unsigned char map[48];
static volatile unsigned char occupied[128];

// ====== fixture senza CRT e confronto integrale dello stato prima di un rifiuto ===== //

static void clear(void *object, BOOT_U64 bytes)
{
    volatile unsigned char *data = object;
    BOOT_U64 index;
    for (index = 0; index < bytes; ++index) data[index] = 0;
}

static int equal(const void *first, const void *second, BOOT_U64 bytes)
{
    const unsigned char *a = first, *b = second;
    BOOT_U64 index;
    for (index = 0; index < bytes; ++index) if (a[index] != b[index]) return 0;
    return 1;
}

static int initialize(BOOT_U64 base, BOOT_U64 pages)
{
    BOOT_U32 index;
    clear(&owner, sizeof(owner)); clear(map, sizeof(map));
    map[0] = 7; map[32] = 8;
    for (index = 0; index < 8; ++index) {
        map[8 + index] = (unsigned char)(base >> (index * 8));
        map[24 + index] = (unsigned char)(pages >> (index * 8));
    }
    return kernel_physical_initialize(&owner, map, sizeof(map), sizeof(map), 32);
}

// ====== fotografia volatile dei metadati senza importare memcpy nel test freestanding ===== //

static void capture(void)
{
    volatile unsigned char *to = (volatile unsigned char *)&snapshot;
    const unsigned char *from = (const unsigned char *)&owner;
    BOOT_U64 index;
    for (index = 0; index < sizeof(owner); ++index) to[index] = from[index];
}

// ====== split interno conserva prefisso e suffisso e il rilascio li riunisce ===== //

static int split_and_merge(void)
{
    KERNEL_PHYSICAL_LEASE first = {0}, prefix = {0}, suffix = {0}, stale;
    if (!initialize(0x1000, 15) || !kernel_physical_allocate_aligned(&owner, 3, 4, &first) ||
        first.Base != 0x4000 || owner.FreePages != 12 || owner.Count != 2 ||
        owner.Free[0].Base != 0x1000 || owner.Free[0].Pages != 3 ||
        owner.Free[1].Base != 0x7000 || owner.Free[1].Pages != 9) return 1;
    if (!kernel_physical_allocate_aligned(&owner, 3, 1, &prefix) || prefix.Base != 0x1000 ||
        !kernel_physical_allocate_aligned(&owner, 9, 1, &suffix) || suffix.Base != 0x7000 ||
        owner.Count || owner.FreePages || !kernel_physical_validate(&owner)) return 2;
    if (!kernel_physical_pin(&owner, &first, 1)) return 3;
    capture();
    if (kernel_physical_release(&owner, &first) || !equal(&owner, &snapshot, sizeof(owner)) ||
        !kernel_physical_pin(&owner, &first, 0)) return 4;
    stale = first;
    if (!kernel_physical_release(&owner, &prefix) || !kernel_physical_release(&owner, &suffix) ||
        !kernel_physical_release(&owner, &first) || owner.Count != 1 || owner.FreePages != 15 ||
        owner.Free[0].Base != 0x1000 || owner.Free[0].Pages != 15) return 5;
    if (!kernel_physical_allocate_aligned(&owner, 3, 4, &first) || first.Id == stale.Id ||
        kernel_physical_release(&owner, &stale) || !kernel_physical_release(&owner, &first)) return 6;
    return 0;
}

// ====== output non vuoti alias overflow e allineamenti invalidi sono transazionali ===== //

static int rejected_requests(void)
{
    KERNEL_PHYSICAL_LEASE output = {0}, before;
    BOOT_U64 bad[] = {0, 3, 5, ~0ULL, 1ULL << 63};
    BOOT_U32 index;
    if (!initialize(0x1000, 15)) return 1;
    capture();
    for (index = 0; index < sizeof(bad) / sizeof(bad[0]); ++index)
        if (kernel_physical_allocate_aligned(&owner, 1, bad[index], &output) ||
            !equal(&owner, &snapshot, sizeof(owner)) || output.Id || output.Base) return 2;
    if (kernel_physical_allocate_aligned(&owner, 0, 1, &output) ||
        kernel_physical_allocate_aligned(&owner, ~0ULL, 1, &output) ||
        kernel_physical_allocate_aligned(&owner, 1, 16, &output) ||
        kernel_physical_allocate_aligned(&owner, 1, 1, &owner.Live[0]) ||
        !equal(&owner, &snapshot, sizeof(owner))) return 3;
    output.Base = 0x7000; before = output;
    if (kernel_physical_allocate_aligned(&owner, 1, 1, &output) ||
        !equal(&before, &output, sizeof(output)) || !equal(&owner, &snapshot, sizeof(owner))) return 4;
    clear(&output, sizeof(output)); owner.NextId = ~0ULL; capture();
    if (kernel_physical_allocate_aligned(&owner, 1, 1, &output) ||
        !equal(&owner, &snapshot, sizeof(owner))) return 5;
    if (!initialize(0xfffff000ULL, 1)) return 6;
    capture();
    if (kernel_physical_allocate_aligned(&owner, 1, 2, &output) ||
        !equal(&owner, &snapshot, sizeof(owner)) ||
        !kernel_physical_allocate_aligned(&owner, 1, 1, &output) || output.Base != 0xfffff000ULL ||
        !kernel_physical_release(&owner, &output)) return 7;
    return 0;
}

// ====== capacita esatta degli extent senza scrivere oltre i metadati ===== //

static int full_metadata(void)
{
    BOOT_U32 index;
    KERNEL_PHYSICAL_LEASE output = {0}, live;
    clear(&owner, sizeof(owner));
    owner.Self = (BOOT_U64)&owner; owner.Ready = 1; owner.PhysicalBits = 32;
    owner.NextId = 2; owner.Count = KERNEL_PHYSICAL_EXTENTS;
    for (index = 0; index < owner.Count; ++index)
        owner.Free[index] = (KERNEL_PHYSICAL_EXTENT){0x1000 + (BOOT_U64)index * 0x10000, 1};
    owner.Free[0].Pages = 7;
    owner.TotalPages = owner.FreePages = KERNEL_PHYSICAL_EXTENTS + 6;
    if (!kernel_physical_validate(&owner)) return 1;
    capture();
    if (kernel_physical_allocate_aligned(&owner, 1, 4, &output) ||
        !equal(&owner, &snapshot, sizeof(owner))) return 2;
    // Il primo candidato richiede uno split: proseguire verso un extent consumabile.
    owner.Free[1].Base = 0x10000;
    if (!kernel_physical_allocate_aligned(&owner, 1, 4, &output) || output.Base != 0x10000 ||
        owner.Count != KERNEL_PHYSICAL_EXTENTS - 1 || !kernel_physical_release(&owner, &output)) return 5;
    owner.Free[1].Base = 0x11000;
    // Una richiesta che consuma la fine non richiede un record aggiuntivo.
    if (!kernel_physical_allocate_aligned(&owner, 4, 4, &output) || output.Base != 0x4000 ||
        owner.Count != KERNEL_PHYSICAL_EXTENTS || !kernel_physical_release(&owner, &output) ||
        !kernel_physical_validate(&owner)) return 3;
    // A capacita piena, una fusione con il solo vicino destro non sposta l'array.
    live = (KERNEL_PHYSICAL_LEASE){0x10000, 1, owner.NextId++, (BOOT_U64)&owner}; owner.Live[0] = live;
    ++owner.TotalPages;
    if (!kernel_physical_validate(&owner) || !kernel_physical_release(&owner, &live) ||
        owner.Count != KERNEL_PHYSICAL_EXTENTS || owner.Free[1].Base != 0x10000 || owner.Free[1].Pages != 2) return 6;
    owner.Free[1] = (KERNEL_PHYSICAL_EXTENT){0x11000, 1};
    --owner.TotalPages; --owner.FreePages;
    owner.Free[0].Pages = 1; owner.Free[1].Base = 0x3000;
    owner.TotalPages = KERNEL_PHYSICAL_EXTENTS + 1; owner.FreePages = KERNEL_PHYSICAL_EXTENTS;
    live = (KERNEL_PHYSICAL_LEASE){0x9000, 1, owner.NextId++, (BOOT_U64)&owner}; owner.Live[0] = live;
    if (!kernel_physical_validate(&owner)) return 7;
    capture();
    if (kernel_physical_release(&owner, &live) || !equal(&owner, &snapshot, sizeof(owner)) || live.Base != 0x9000) return 8;
    live.Base = 0x2000; owner.Live[0] = live;
    if (!kernel_physical_validate(&owner) || !kernel_physical_release(&owner, &live) ||
        owner.Count != KERNEL_PHYSICAL_EXTENTS - 1 || owner.Free[0].Pages != 3 ||
        owner.FreePages != owner.TotalPages || !kernel_physical_validate(&owner)) return 4;
    return 0;
}

// ====== first fit indipendente tramite bitmap senza arrotondamenti condivisi ===== //

static BOOT_U64 expected(BOOT_U64 pages, BOOT_U64 alignment)
{
    BOOT_U32 index, offset;
    for (index = 0; index < 128; ++index) {
        if ((index + 1) % alignment || pages > 128 - index) continue;
        for (offset = 0; offset < pages && !occupied[index + offset]; ++offset) { }
        if (offset == pages) return (index + 1ULL) * 4096;
    }
    return 0;
}

static int mark(const KERNEL_PHYSICAL_LEASE *lease, unsigned char value)
{
    BOOT_U64 first = lease->Base / 4096 - 1, offset;
    if (first >= 128 || lease->Pages > 128 - first) return 0;
    for (offset = 0; offset < lease->Pages; ++offset)
        if (occupied[first + offset] == value) return 0;
    for (offset = 0; offset < lease->Pages; ++offset) occupied[first + offset] = value;
    return 1;
}

// ====== sequenza mista di 8192 richieste e rilasci con confronto ad ogni operazione ===== //

static int mixed_sequences(void)
{
    BOOT_U32 step, slot, index, seed = 937;
    BOOT_U64 used = 0;
    if (!initialize(0x1000, 128)) return 1;
    for (index = 0; index < 128; ++index) occupied[index] = 0;
    clear(leases, sizeof(leases));
    for (step = 0; step < 8192; ++step) {
        seed = seed * 1664525U + 1013904223U;
        slot = (seed >> 24) & 63;
        if (leases[slot].Id) {
            BOOT_U64 pages = leases[slot].Pages;
            if (!mark(&leases[slot], 0) || !kernel_physical_release(&owner, &leases[slot])) return 2;
            used -= pages;
        } else {
            BOOT_U64 pages = 1 + ((seed >> 16) & 7), alignment = 1ULL << ((seed >> 8) & 7);
            BOOT_U64 base = expected(pages, alignment);
            capture();
            if (kernel_physical_allocate_aligned(&owner, pages, alignment, &leases[slot]) != (base != 0)) return 3;
            if (base) {
                if (leases[slot].Base != base || leases[slot].Pages != pages || !mark(&leases[slot], 1)) return 4;
                used += pages;
            } else if (!equal(&owner, &snapshot, sizeof(owner)) || leases[slot].Id) return 5;
        }
        if (owner.FreePages != 128 - used || !kernel_physical_validate(&owner)) return 6;
        for (index = 0; index < owner.Count; ++index) {
            BOOT_U64 first = owner.Free[index].Base / 4096 - 1, offset;
            if (first >= 128 || owner.Free[index].Pages > 128 - first) return 7;
            for (offset = 0; offset < owner.Free[index].Pages; ++offset) if (occupied[first + offset]) return 8;
        }
    }
    for (slot = 0; slot < 64; ++slot) if (leases[slot].Id && !kernel_physical_release(&owner, &leases[slot])) return 9;
    return owner.Count == 1 && owner.FreePages == 128 && owner.Free[0].Base == 4096 &&
        owner.Free[0].Pages == 128 && kernel_physical_validate(&owner) ? 0 : 10;
}

// ====== entry host con codici che identificano il gruppo fallito ===== //

__declspec(dllexport) int run_alignment_tests(void)
{
    int result = split_and_merge();
    if (result) return 100 + result;
    result = rejected_requests(); if (result) return 200 + result;
    result = full_metadata(); if (result) return 300 + result;
    result = mixed_sequences(); return result ? 400 + result : 0;
}
