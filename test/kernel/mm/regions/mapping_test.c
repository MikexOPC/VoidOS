#include "arch/x86/mm/regions/mapping.h"
#include "arch/x86/configs/layout.h"
static X86_REGION_MAPPER owner, copy;
static KERNEL_PHYSICAL_ALLOCATOR pool;
static BOOT_U64 tables[4][512];
_Alignas(4096) static volatile unsigned char buffers[4][32 * 4096];
static BOOT_U32 missing, byte_offset, invalidations, ordering;
static unsigned char map[48];

// ====== backend indipendente con verifica del pin durante ogni invalidazione ===== //

volatile BOOT_U64 *x86_region_entries(BOOT_U32 level) { return level < 4 ? tables[level] : 0; }
volatile unsigned char *x86_region_bytes(BOOT_U32 slot) { return slot < 4 && !missing ? buffers[slot] + byte_offset : 0; }
void x86_region_invalidate(BOOT_U64 address)
{
    BOOT_U32 slot, index;
    for (slot = 0; slot < 4; ++slot) {
        BOOT_U64 base = x86_region_base(slot);
        if (address >= base && address < base + owner.Live[slot].Pages * 4096) {
            int present = tables[3][128 + slot * 34 + 1] != 0;
            if ((address & 4095) || !kernel_physical_is_pinned(&pool, &owner.Live[slot])) ordering = 1;
            for (index = 0; index < owner.Live[slot].Pages; ++index)
                if ((tables[3][128 + slot * 34 + 1 + index] != 0) != present) ordering = 1;
            ++invalidations;
            return;
        }
    }
    ordering = 1;
}

// ====== oggetti vergini e ramo ricorsivo preparati senza memoria privilegiata ===== //

static void clear(void *object, BOOT_U64 bytes)
{
    BOOT_U64 index;
    for (index = 0; index < bytes; ++index) ((volatile unsigned char *)object)[index] = 0;
}

static int fixture(void)
{
    clear(&owner, sizeof(owner)); clear(&pool, sizeof(pool)); clear(tables, sizeof(tables)); clear(map, sizeof(map));
    missing = byte_offset = invalidations = ordering = 0;
    map[0] = 7; map[10] = 0x10; map[24] = 128; map[32] = 8;
    tables[0][X86_LAYOUT_RECURSIVE_SLOT] = 0x8000000000001003ULL;
    tables[0][X86_LAYOUT_WINDOWS_SLOT] = 0x2003; tables[1][0] = 0x3003; tables[2][0] = 0x4003;
    return kernel_physical_initialize(&pool, map, 48, 48, 32) && x86_region_initialize(&owner, &pool, 0x1000);
}

// ====== fixture condivisa con i test di binding del pool senza duplicare il backend ===== //

int x86_test_region_fixture(void) { return fixture(); }
X86_REGION_MAPPER *x86_test_region_mapper(void) { return &owner; }
KERNEL_PHYSICAL_ALLOCATOR *x86_test_region_pool(void) { return &pool; }
void x86_test_region_offset(BOOT_U32 value) { byte_offset = value; }

// ====== quattro mapping simultanei con saturazione pin riuso e isolamento dei byte ===== //

static int simultaneous(void)
{
    X86_REGION_HANDLE handles[4] = {0}, extra = {0}, stale;
    BOOT_U32 slot, byte;
    if (!fixture()) return 1;
    for (slot = 0; slot < 4; ++slot) {
        volatile unsigned char *data;
        if (!x86_region_acquire(&owner, 32, 16, &handles[slot]) ||
            handles[slot].Virtual != x86_region_base(slot) || (owner.Live[slot].Base & 65535)) return 2;
        data = x86_region_data(&owner, &handles[slot]);
        if (!data) return 3;
        for (byte = 0; byte < 32 * 4096; ++byte) { if (data[byte]) return 4; data[byte] = (unsigned char)(slot + 1); }
    }
    if (pool.FreePages || x86_region_acquire(&owner, 1, 1, &extra) || extra.Id || !x86_region_validate(&owner)) return 5;
    for (slot = 0; slot < 4; ++slot) {
        if (kernel_physical_release(&pool, &owner.Live[slot])) return 6;
        for (byte = 0; byte < 32 * 4096; ++byte) if (buffers[slot][byte] != slot + 1) return 7;
    }
    stale = handles[1];
    if (!x86_region_release(&owner, &handles[1]) || x86_region_data(&owner, &stale) ||
        !x86_region_acquire(&owner, 32, 16, &handles[1]) || handles[1].Id == stale.Id ||
        x86_region_release(&owner, &stale)) return 8;
    for (byte = 0; byte < 32 * 4096; ++byte) if (buffers[1][byte]) return 9;
    for (slot = 0; slot < 4; ++slot) if (!x86_region_release(&owner, &handles[slot])) return 10;
    if (pool.FreePages != 128 || !x86_region_validate(&owner) || ordering || invalidations != 320) return 11;
    return 0;
}

// ====== errori prima della pubblicazione e rollback dopo un backend di zero assente ===== //

static int rollback(void)
{
    X86_REGION_HANDLE handle = {0};
    KERNEL_PHYSICAL_LEASE held = {0};
    BOOT_U64 budget, id;
    if (!fixture()) return 1;
    budget = pool.FreePages; id = pool.NextId;
    if (x86_region_acquire(&owner, 0, 1, &handle) || x86_region_acquire(&owner, 33, 1, &handle) ||
        x86_region_acquire(&owner, 1, 3, &handle) || pool.FreePages != budget || pool.NextId != id || handle.Id) return 2;
    if (!kernel_physical_allocate(&pool, 100, &held)) return 8;
    id = pool.NextId;
    if (x86_region_acquire(&owner, 32, 1, &handle) || handle.Id || pool.FreePages != 28 ||
        pool.NextId != id || invalidations || !kernel_physical_release(&pool, &held)) return 9;
    missing = 1;
    if (x86_region_acquire(&owner, 4, 1, &handle) || handle.Id || pool.FreePages != budget ||
        !x86_region_validate(&owner) || ordering || invalidations != 8) return 3;
    missing = 0;
    if (!x86_region_acquire(&owner, 4, 1, &handle)) return 4;
    {
        BOOT_U32 byte;
        for (byte = 0; byte < sizeof(copy); ++byte) ((volatile unsigned char *)&copy)[byte] = ((const unsigned char *)&owner)[byte];
    }
    if (x86_region_validate(&copy) || x86_region_release(&copy, &handle)) return 5;
    handle.Pages = 3;
    if (x86_region_data(&owner, &handle) || x86_region_release(&owner, &handle)) return 6;
    handle.Pages = 4;
    if (!x86_region_release(&owner, &handle) || pool.FreePages != budget) return 7;
    return 0;
}

// ====== ogni bit non A D di 136 PTE e guardie respinto prima di qualsiasi unmap ===== //

static int corruptions(void)
{
    X86_REGION_HANDLE handle = {0};
    BOOT_U32 slot, bit;
    if (!fixture() || !x86_region_acquire(&owner, 32, 1, &handle)) return 1;
    for (slot = 128; slot < 264; ++slot) {
        BOOT_U64 original = tables[3][slot];
        for (bit = 0; bit < 64; ++bit) {
            int permitted = original && (bit == 5 || bit == 6);
            BOOT_U32 before = invalidations;
            tables[3][slot] = original ^ (1ULL << bit);
            if (x86_region_validate(&owner) != permitted) return 2;
            if (!permitted && (x86_region_release(&owner, &handle) || invalidations != before || pool.FreePages != 96)) return 3;
            tables[3][slot] = original;
        }
    }
    if (!x86_region_release(&owner, &handle)) return 4;
    return ordering ? 5 : 0;
}

// ====== genitori e recusione corrotti non raggiungono foglie modificabili ===== //

static int parents(void)
{
    const BOOT_U32 indices[] = {X86_LAYOUT_WINDOWS_SLOT, 0, 0};
    BOOT_U32 level, bit;
    if (!fixture()) return 1;
    for (level = 0; level < 3; ++level) {
        BOOT_U64 original = tables[level][indices[level]];
        for (bit = 0; bit < 64; ++bit) {
            tables[level][indices[level]] = original ^ (1ULL << bit);
            // Alterare la base puo ancora descrivere un altro ramo valido, non la nostra fixture.
            if (bit != 5 && bit != 6 && (bit < 12 || bit >= 32) && x86_region_validate(&owner)) return 2;
            tables[level][indices[level]] = original;
        }
    }
    tables[0][128] ^= 1ULL << 63;
    if (x86_region_validate(&owner)) return 3;
    return 0;
}

// ====== cicli di vita con riuso delle pagine ma identificatori sempre diversi ===== //

static int cycles(void)
{
    BOOT_U32 round;
    BOOT_U64 last = 0;
    X86_REGION_HANDLE handle = {0};
    if (!fixture()) return 1;
    for (round = 0; round < 128; ++round) {
        if (!x86_region_acquire(&owner, 1 + round % 32, 1ULL << (round % 5), &handle) ||
            handle.Id <= last || !x86_region_data(&owner, &handle)) return 2;
        last = handle.Id;
        if (!x86_region_release(&owner, &handle) || pool.FreePages != 128 || handle.Id ||
            !x86_region_validate(&owner)) return 3;
    }
    return ordering ? 4 : 0;
}

// ====== metadata alias e output non vuoto respinti senza consumo di pagine ===== //

static int invalid_handles(void)
{
    X86_REGION_HANDLE handle = {0}, bad;
    if (!fixture()) return 1;
    if (x86_region_acquire(&owner, 1, 1, (X86_REGION_HANDLE *)&owner.Live[0]) ||
        x86_region_acquire(&owner, 1, 1, (X86_REGION_HANDLE *)&pool.Live[0]) || pool.FreePages != 128) return 2;
    handle.Id = 73;
    if (x86_region_acquire(&owner, 1, 1, &handle) || handle.Id != 73 || pool.FreePages != 128) return 3;
    handle.Id = 0;
    if (!x86_region_acquire(&owner, 1, 1, &handle)) return 4;
    if (!x86_region_bind(&owner, &handle, (BOOT_U64)&copy) ||
        x86_region_bind(&owner, &handle, (BOOT_U64)&copy) || x86_region_release(&owner, &handle) ||
        x86_region_unbind(&owner, &handle, (BOOT_U64)&pool) ||
        !x86_region_unbind(&owner, &handle, (BOOT_U64)&copy)) return 12;
    if (x86_region_detach(&owner)) return 8;
    bad = handle; ++bad.Virtual;
    if (x86_region_data(&owner, &bad) || x86_region_release(&owner, &bad)) return 5;
    bad = handle; bad.Owner = 0;
    if (x86_region_data(&owner, &bad) || x86_region_release(&owner, &bad)) return 6;
    bad = handle;
    if (!x86_region_release(&owner, &handle) || !x86_region_detach(&owner) || owner.Ready ||
        !x86_region_initialize(&owner, &pool, 0x1000) || !x86_region_acquire(&owner, 1, 1, &handle) ||
        handle.Id == bad.Id || x86_region_data(&owner, &bad) || !x86_region_release(&owner, &handle)) return 9;
    if (!x86_region_detach(&owner)) return 10;
    tables[3][128] = 1;
    if (x86_region_initialize(&owner, &pool, 0x1000) || owner.Ready) return 11;
    return pool.FreePages == 128 ? 0 : 7;
}

// ====== entry host con identificazione del gruppo fallito ===== //

__declspec(dllexport) int run_region_tests(void)
{
    int result = simultaneous();
    if (result) return 100 + result;
    result = rollback(); if (result) return 200 + result;
    result = corruptions(); if (result) return 300 + result;
    result = parents(); if (result) return 400 + result;
    result = cycles(); if (result) return 500 + result;
    result = invalid_handles(); return result ? 600 + result : 0;
}
