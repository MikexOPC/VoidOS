#include "page.h"
#include "access.h"
#include "kernel/mm/inventory.h"
#include "lib/memory/address_ranges.h"
#define PAGE_NX (1ULL << 63)
#define PAGE_ADDRESS 0x000ffffffffff000ULL
#define PAGE_AD 0x60ULL
#define WINDOW_LEAF ((X86_KERNEL_PAGE_WINDOW >> 12) & 511)
BOOT_U64 x86_window_span(BOOT_U64 pages);
BOOT_U64 x86_window_frame(BOOT_U64 base, BOOT_U64 pages, BOOT_U32 bits);

// ====== verifica dei genitori prima di accedere al livello seguente ===== //

static int tables_valid(BOOT_U64 root, BOOT_U32 bits)
{
    BOOT_U32 level;
    const BOOT_U32 shifts[] = {39, 30, 21};
    volatile BOOT_U64 *entries;
    if (bits < 32 || bits > 52 || !root || (root & 4095) ||
        root >= (1ULL << bits) || root != x86_kernel_root()) {
        return 0;
    }
    entries = x86_window_entries(0);
    if (!entries || (entries[X86_BOOT_RECURSIVE_SLOT] & ~PAGE_AD) != (root | 3 | PAGE_NX)) {
        return 0;
    }
    for (level = 0; level < 3; ++level) {
        BOOT_U64 entry, physical;
        entries = x86_window_entries(level);
        if (!entries) {
            return 0;
        }
        entry = entries[(X86_KERNEL_PAGE_WINDOW >> shifts[level]) & 511];
        physical = entry & PAGE_ADDRESS;
        if (!physical || physical >= (1ULL << bits) ||
            (entry & ~PAGE_AD) != (physical | 3)) {
            return 0;
        }
    }
    entries = x86_window_entries(3);
    return entries && !entries[WINDOW_LEAF - 1] &&
           !entries[WINDOW_LEAF + X86_KERNEL_WINDOW_PAGES];
}

// ====== stato completo della finestra controllato prima delle mutazioni ===== //

static int window_valid(const X86_PAGE_WINDOW *window)
{
    if (!window || window->Self != (BOOT_U64)window || window->Ready != 1 ||
        window->Active > 1 || !window->Pool || !kernel_physical_validate(window->Pool)) {
        return 0;
    }
    if (!tables_valid(window->Root, window->Pool->PhysicalBits)) {
        return 0;
    }
    if (!window->Active) {
        return !window->Lease.Base && !window->Lease.Pages && !window->Lease.Id && !window->Lease.Owner;
    }
    return x86_window_frame(window->Lease.Base, window->Lease.Pages, window->Pool->PhysicalBits) != 0 &&
           kernel_physical_is_pinned(window->Pool, &window->Lease);
}

// ====== tutte le PTE e la coda inutilizzata verificate prima del rilascio ===== //

static int leaves_match(const X86_PAGE_WINDOW *window)
{
    BOOT_U64 index;
    volatile BOOT_U64 *entries = x86_window_entries(3);
    if (!entries) {
        return 0;
    }
    for (index = 0; index < X86_KERNEL_WINDOW_PAGES; ++index) {
        BOOT_U64 expected = 0;
        if (window->Active && index < window->Lease.Pages) {
            expected = (window->Lease.Base + index * 4096) | 3 | PAGE_NX;
        }
        if (expected) {
            if ((entries[WINDOW_LEAF + index] & ~PAGE_AD) != expected) return 0;
        } else if (entries[WINDOW_LEAF + index]) {
            return 0;
        }
    }
    return 1;
}

// ====== controllo completo riusato dai prestiti MMIO senza possedere frame ===== //

int x86_page_window_idle_valid(const X86_PAGE_WINDOW *window)
{
    return window_valid(window) && !window->Active && leaves_match(window);
}

// ====== owner non spostabile e distinto dai metadati fisici ===== //

int x86_page_window_initialize(X86_PAGE_WINDOW *window, KERNEL_PHYSICAL_ALLOCATOR *pool, BOOT_U64 root)
{
    BOOT_U64 index;
    BOOT_U64 a = (BOOT_U64)window, b = (BOOT_U64)pool;
    if (!window || !pool || a > ~0ULL - sizeof(*window) || b > ~0ULL - sizeof(*pool) ||
        (a < b + sizeof(*pool) && b < a + sizeof(*window)) || !kernel_physical_validate(pool)) {
        return 0;
    }
    for (index = 0; index < sizeof(*window); ++index) {
        if (((const unsigned char *)window)[index]) return 0;
    }
    if (!tables_valid(root, pool->PhysicalBits)) {
        return 0;
    }
    for (index = 0; index < X86_KERNEL_WINDOW_PAGES; ++index) {
        if (x86_window_entries(3)[WINDOW_LEAF + index]) return 0;
    }
    window->Self = a;
    window->Root = root;
    window->Pool = pool;
    window->Ready = 1;
    return 1;
}

// ====== invalidazione di ogni pagina dopo il batch di modifiche PTE ===== //

static void invalidate_span(BOOT_U64 pages)
{
    BOOT_U64 index;
    for (index = 0; index < pages; ++index) {
        x86_invalidate_page(X86_KERNEL_PAGE_WINDOW + index * 4096);
    }
}

// ====== azzeramento con dimensione esplicita prima del successo ===== //

static int zero_and_verify(volatile unsigned char *data, BOOT_U64 bytes)
{
    BOOT_U64 index;
    if (!data || !bytes || bytes > X86_KERNEL_WINDOW_BYTES) {
        return 0;
    }
    for (index = 0; index < bytes; ++index) {
        data[index] = 0;
    }
    for (index = 0; index < bytes; ++index) {
        if (data[index]) return 0;
    }
    return 1;
}

// ====== allocazione unica e rollback prima della pubblicazione delle PTE ===== //

static int allocate_and_pin(X86_PAGE_WINDOW *window, BOOT_U64 pages, BOOT_U64 bytes)
{
    if (!kernel_physical_allocate(window->Pool, pages, &window->Lease)) {
        return 0;
    }
    if (x86_window_frame(window->Lease.Base, pages, window->Pool->PhysicalBits) != bytes ||
        !kernel_physical_pin(window->Pool, &window->Lease, 1)) {
        kernel_physical_release(window->Pool, &window->Lease);
        return 0;
    }
    return 1;
}

// ====== assegnazione contigua supervisor RW NX con budget limitato ===== //

int x86_page_window_acquire(X86_PAGE_WINDOW *window, BOOT_U64 pages)
{
    volatile unsigned char *data;
    volatile BOOT_U64 *entries;
    BOOT_U64 index, bytes = x86_window_span(pages);
    if (!bytes || !window_valid(window) || window->Active || !leaves_match(window)) {
        return 0;
    }
    data = x86_window_bytes();
    if (!data || !allocate_and_pin(window, pages, bytes)) {
        return 0;
    }
    entries = x86_window_entries(3);
    for (index = 0; index < pages; ++index) {
        entries[WINDOW_LEAF + index] = (window->Lease.Base + index * 4096) | 3 | PAGE_NX;
    }
    invalidate_span(pages);
    window->Active = 1;
    if (!zero_and_verify(data, bytes)) {
        x86_page_window_release(window);
        return 0;
    }
    return 1;
}

// ====== unmap completo e invalidazione prima di restituire il blocco fisico ===== //

int x86_page_window_release(X86_PAGE_WINDOW *window)
{
    BOOT_U64 index, pages;
    volatile BOOT_U64 *entries;
    if (!window_valid(window) || !window->Active ||
        window->Pool->Count == KERNEL_PHYSICAL_EXTENTS || !leaves_match(window)) {
        return 0;
    }
    pages = window->Lease.Pages;
    entries = x86_window_entries(3);
    for (index = 0; index < pages; ++index) {
        entries[WINDOW_LEAF + index] = 0;
    }
    invalidate_span(pages);
    if (!kernel_physical_pin(window->Pool, &window->Lease, 0) ||
        !kernel_physical_release(window->Pool, &window->Lease)) {
        return 0;
    }
    window->Active = 0;
    return 1;
}

// ====== copertura di tutte le pagine esposte dal prestito ACPI ===== //

static int acpi_range(const KERNEL_BOOT_CONTEXT *context, BOOT_U64 base, BOOT_U64 span)
{
    KERNEL_MEMORY_INVENTORY inventory;
    BOOT_U64 index;
    BOOT_U32 found = 0;
    if (!kernel_inventory_memory(context->MemoryMap, context->Info.MemoryMapBytes,
                                  context->Info.MemoryDescriptorBytes, &inventory)) return 0;
    for (index = 0; index < inventory.Regions; ++index) {
        KERNEL_MEMORY_REGION region;
        BOOT_U64 bytes;
        if (!kernel_read_region(context->MemoryMap, context->Info.MemoryMapBytes,
                                context->Info.MemoryDescriptorBytes, index, &region)) return 0;
        bytes = region.Pages * 4096;
        if (!os_address_ranges_overlap(base, span, region.Base, bytes)) continue;
        if ((region.Type != 9 && region.Type != 10) || !(region.Attributes & 8) ||
            (region.Attributes & 0x2000) || base < region.Base || base - region.Base >= bytes ||
            span > bytes - (base - region.Base)) return 0;
        ++found;
    }
    return found == 1;
}

// ====== prestito supervisor R NX senza lease ne allocazioni nuove ===== //

int x86_page_window_read_acpi(X86_PAGE_WINDOW *window, const KERNEL_BOOT_CONTEXT *context,
                              BOOT_U64 physical, BOOT_U64 bytes, unsigned char *output)
{
    BOOT_U64 base, span, copied = 0, address = (BOOT_U64)output;
    volatile BOOT_U64 *entries;
    volatile unsigned char *source;
    if (!context || !output || !bytes || bytes > 65536 || address > ~0ULL - bytes ||
        !window_valid(window) || window->Active || !leaves_match(window) ||
        !kernel_validate_contract(&context->Info, window->Root, context->Info.KernelEntry)) return 0;
    if ((BOOT_U64)context->MemoryMap > ~0ULL - context->Info.MemoryMapCapacity) return 0;
    if (physical < 4096 || physical >= (1ULL << window->Pool->PhysicalBits) ||
        bytes > (1ULL << window->Pool->PhysicalBits) - physical) return 0;
    base = physical & ~4095ULL;
    span = ((physical - base + bytes + 4095) / 4096) * 4096;
    if (os_address_ranges_overlap(address, bytes, (BOOT_U64)window, sizeof(*window)) ||
        os_address_ranges_overlap(address, bytes, (BOOT_U64)window->Pool, sizeof(*window->Pool)) ||
        os_address_ranges_overlap(address, bytes, (BOOT_U64)context, sizeof(*context)) ||
        os_address_ranges_overlap(address, bytes, (BOOT_U64)context->MemoryMap, context->Info.MemoryMapCapacity) ||
        os_address_ranges_overlap(address, bytes, X86_KERNEL_PAGE_WINDOW, X86_KERNEL_WINDOW_BYTES) ||
        !acpi_range(context, base, span)) return 0;
    source = x86_window_bytes();
    entries = x86_window_entries(3);
    if (!source || !entries) return 0;
    // UP e IF=0: nessun altro utilizzatore puo entrare durante il prestito.
    while (copied < bytes) {
        BOOT_U64 current = physical + copied, offset = current & 4095, index;
        BOOT_U64 amount = bytes - copied;
        BOOT_U64 expected = (current & ~4095ULL) | 1 | PAGE_NX;
        if (amount > 4096 - offset) amount = 4096 - offset;
        entries[WINDOW_LEAF] = expected;
        x86_invalidate_page(X86_KERNEL_PAGE_WINDOW);
        if ((entries[WINDOW_LEAF] & ~0x20ULL) != expected) {
            entries[WINDOW_LEAF] = 0;
            x86_invalidate_page(X86_KERNEL_PAGE_WINDOW);
            return 0;
        }
        for (index = 0; index < amount; ++index) output[copied + index] = source[offset + index];
        entries[WINDOW_LEAF] = 0;
        x86_invalidate_page(X86_KERNEL_PAGE_WINDOW);
        copied += amount;
    }
    return leaves_match(window);
}
