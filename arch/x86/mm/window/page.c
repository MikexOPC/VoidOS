#include "page.h"
#include "access.h"
#define PAGE_NX (1ULL << 63)
#define PAGE_ADDRESS 0x000ffffffffff000ULL
#define PAGE_AD 0x60ULL
#define WINDOW_LEAF ((X86_KERNEL_PAGE_WINDOW >> 12) & 511)

// ====== verifica dei genitori prima di accedere al livello seguente ====== //

static int tables_valid(BOOT_U64 root, BOOT_U32 bits)
{
    BOOT_U32 level;
    const BOOT_U32 shifts[] = {39, 30, 21};
    volatile BOOT_U64 *entries;
    if (bits < 32 || bits > 52 || !root || (root & 4095) || root >= (1ULL << bits) || root != x86_kernel_root()) return 0;
    entries = x86_window_entries(0);
    if (!entries || (entries[X86_BOOT_RECURSIVE_SLOT] & ~PAGE_AD) != (root | 3 | PAGE_NX)) return 0;
    for (level = 0; level < 3; ++level) {
        BOOT_U64 entry, physical;
        entries = x86_window_entries(level);
        if (!entries) return 0;
        entry = entries[(X86_KERNEL_PAGE_WINDOW >> shifts[level]) & 511];
        physical = entry & PAGE_ADDRESS;
        if (!physical || physical >= (1ULL << bits) || (entry & ~PAGE_AD) != (physical | 3)) return 0;
    }
    entries = x86_window_entries(3);
    return entries && !entries[WINDOW_LEAF - 1] && !entries[WINDOW_LEAF + 1];
}

// ====== owner non spostabile e record estraneo ai metadati fisici ====== //

int x86_page_window_initialize(X86_PAGE_WINDOW *window, KERNEL_PHYSICAL_ALLOCATOR *pool, BOOT_U64 root)
{
    BOOT_U32 index;
    BOOT_U64 a = (BOOT_U64)window, b = (BOOT_U64)pool;
    if (!window || !pool || a > ~0ULL - sizeof(*window) || b > ~0ULL - sizeof(*pool) ||
        (a < b + sizeof(*pool) && b < a + sizeof(*window)) || !kernel_physical_validate(pool)) return 0;
    for (index = 0; index < sizeof(*window); ++index) if (((const unsigned char *)window)[index]) return 0;
    if (!tables_valid(root, pool->PhysicalBits) || x86_window_entries(3)[WINDOW_LEAF]) return 0;
    window->Self = a; window->Root = root; window->Pool = pool; window->Ready = 1;
    return 1;
}

// ====== pubblicazione supervisor RW NX e azzeramento prima del successo ====== //

int x86_page_window_acquire(X86_PAGE_WINDOW *window)
{
    volatile unsigned char *bytes;
    BOOT_U32 index;
    if (!window || window->Self != (BOOT_U64)window || window->Ready != 1 || window->Active ||
        !window->Pool || !tables_valid(window->Root, window->Pool->PhysicalBits) || x86_window_entries(3)[WINDOW_LEAF]) return 0;
    bytes = x86_window_bytes();
    if (!bytes || !kernel_physical_allocate(window->Pool, 1, &window->Lease)) return 0;
    if (!kernel_physical_pin(window->Pool, &window->Lease, 1)) {
        kernel_physical_release(window->Pool, &window->Lease);
        return 0;
    }
    x86_window_entries(3)[WINDOW_LEAF] = window->Lease.Base | 3 | PAGE_NX;
    x86_invalidate_page(X86_KERNEL_PAGE_WINDOW);
    window->Active = 1;
    for (index = 0; index < 4096; ++index) bytes[index] = 0;
    for (index = 0; index < 4096; ++index) if (bytes[index]) { x86_page_window_release(window); return 0; }
    return 1;
}

// ====== PTE rimossa e TLB invalidato prima di sbloccare e liberare il frame ====== //

int x86_page_window_release(X86_PAGE_WINDOW *window)
{
    BOOT_U64 entry;
    if (!window || window->Self != (BOOT_U64)window || window->Ready != 1 || window->Active != 1 || !window->Pool ||
        window->Lease.Pages != 1 || window->Pool->Count == KERNEL_PHYSICAL_EXTENTS ||
        !kernel_physical_is_pinned(window->Pool, &window->Lease) ||
        !tables_valid(window->Root, window->Pool->PhysicalBits)) return 0;
    entry = x86_window_entries(3)[WINDOW_LEAF];
    if ((entry & ~PAGE_AD) != (window->Lease.Base | 3 | PAGE_NX)) return 0;
    x86_window_entries(3)[WINDOW_LEAF] = 0;
    x86_invalidate_page(X86_KERNEL_PAGE_WINDOW);
    if (!kernel_physical_pin(window->Pool, &window->Lease, 0) || !kernel_physical_release(window->Pool, &window->Lease)) return 0;
    window->Active = 0;
    return 1;
}
