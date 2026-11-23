#include "page.h"
#include "access.h"
#ifdef KERNEL_AP_RUNTIME
#include "arch/x86/kernel/cpu/percpu/service/native/native.h"
#include "arch/x86/mm/paging/inventory.h"
#endif
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
        !kernel_physical_release_fits(window->Pool, &window->Lease) || !leaves_match(window)) {
        return 0;
    }
    pages = window->Lease.Pages;
    entries = x86_window_entries(3);
    for (index = 0; index < pages; ++index) {
        entries[WINDOW_LEAF + index] = 0;
    }
#ifdef KERNEL_AP_RUNTIME
    if (xis_native_shootdown()) x86_paging_generation_bump();
    else
#endif
        invalidate_span(pages);
    if (!kernel_physical_release_after_unmap(window->Pool, &window->Lease)) {
        return 0;
    }
    window->Active = 0;
    return 1;
}

// ====== parcheggio: PTE tolte, lease pinned consegnato al chiamante ===== //

int x86_page_window_park(X86_PAGE_WINDOW *window, KERNEL_PHYSICAL_LEASE *parked)
{
    BOOT_U64 index, pages, address = (BOOT_U64)parked;
    volatile BOOT_U64 *entries;

    if (!parked || !window_valid(window) || window->Active != 1 || !leaves_match(window) ||
        address > ~0ULL - sizeof(*parked) ||
        os_address_ranges_overlap(address, sizeof(*parked), (BOOT_U64)window, sizeof(*window)) ||
        os_address_ranges_overlap(address, sizeof(*parked), X86_KERNEL_PAGE_WINDOW, X86_KERNEL_WINDOW_BYTES) ||
        parked->Base || parked->Pages || parked->Id || parked->Owner
    )
    return 0;

    pages = window->Lease.Pages;
    entries = x86_window_entries(3);

    if (!entries) return 0;

    for (index = 0; index < pages; ++index) entries[WINDOW_LEAF + index] = 0;

    invalidate_span(pages);
    *parked = window->Lease;
    window->Lease = (KERNEL_PHYSICAL_LEASE){0};
    window->Active = 0;
    return x86_page_window_idle_valid(window) && kernel_physical_is_pinned(window->Pool, parked);
}

// ====== ripresa dello stesso lease, contenuto conservato ===== //

int x86_page_window_resume(X86_PAGE_WINDOW *window, KERNEL_PHYSICAL_LEASE *parked)
{
    BOOT_U64 index, address = (BOOT_U64)parked;
    volatile BOOT_U64 *entries;

    if (!parked || address > ~0ULL - sizeof(*parked) || !x86_page_window_idle_valid(window) ||
        os_address_ranges_overlap(address, sizeof(*parked), (BOOT_U64)window, sizeof(*window)) ||
        !parked->Pages || parked->Pages > X86_KERNEL_WINDOW_PAGES ||
        x86_window_frame(parked->Base, parked->Pages, window->Pool->PhysicalBits) != x86_window_span(parked->Pages) ||
        !kernel_physical_is_pinned(window->Pool, parked)
    )
    return 0;

    entries = x86_window_entries(3);

    if (!entries) return 0;

    window->Lease = *parked;

    for (index = 0; index < parked->Pages; ++index) {
        entries[WINDOW_LEAF + index] = (window->Lease.Base + index * 4096) | 3 | PAGE_NX;
    }

    invalidate_span(parked->Pages);
    window->Active = 1;

    if (!leaves_match(window)) return 0;

    *parked = (KERNEL_PHYSICAL_LEASE){0};
    return 1;
}

// ====== lease_frame_valid ======

static int lease_frame_valid(const X86_PAGE_WINDOW *window, BOOT_U64 base, BOOT_U64 pages)
{
    if (!window || !window->Pool || !pages || pages > 2048ULL || base < 4096ULL || (base & 4095ULL)) {
        return 0;
    }
    BOOT_U32 bits = window->Pool->PhysicalBits;
    if (bits < 32U || bits > 52U) {
        return 0;
    }
    BOOT_U64 bytes = pages * 4096ULL;
    BOOT_U64 limit = 1ULL << bits;
    return base < limit && bytes <= limit - base;
}

// ====== zero_lease_chunks ======

static int zero_lease_chunks(X86_PAGE_WINDOW *window, const KERNEL_PHYSICAL_LEASE *lease)
{
    volatile unsigned char *data = x86_window_bytes();
    volatile BOOT_U64 *entries = x86_window_entries(3);
    if (!data || !entries) {
        return 0;
    }
    BOOT_U64 done = 0;
    while (done < lease->Pages) {
        BOOT_U64 chunk = lease->Pages - done;
        if (chunk > X86_KERNEL_WINDOW_PAGES) {
            chunk = X86_KERNEL_WINDOW_PAGES;
        }
        for (BOOT_U64 index = 0; index < chunk; ++index) {
            entries[WINDOW_LEAF + index] = (lease->Base + (done + index) * 4096ULL) | 3ULL | PAGE_NX;
        }
        invalidate_span(chunk);
        int ok = zero_and_verify(data, chunk * 4096ULL);
        for (BOOT_U64 index = 0; index < chunk; ++index) {
            entries[WINDOW_LEAF + index] = 0;
        }
        invalidate_span(chunk);
        if (!ok) {
            return 0;
        }
        done += chunk;
    }
    return leaves_match(window);
}

// ====== x86_page_window_allocate_lease ======

int x86_page_window_allocate_lease(X86_PAGE_WINDOW *window, BOOT_U64 pages, KERNEL_PHYSICAL_LEASE *lease_out)
{
    if (!lease_out || !x86_page_window_idle_valid(window) || !pages || pages > 2048ULL ||
        lease_out->Base || lease_out->Pages || lease_out->Id || lease_out->Owner) {
        return 0;
    }
    KERNEL_PHYSICAL_LEASE lease = {0};
    if (!kernel_physical_allocate(window->Pool, pages, &lease)) {
        return 0;
    }
    if (!lease_frame_valid(window, lease.Base, lease.Pages) ||
        !kernel_physical_pin(window->Pool, &lease, 1)) {
        (void)kernel_physical_release(window->Pool, &lease);
        return 0;
    }
    if (!zero_lease_chunks(window, &lease)) {
        (void)kernel_physical_release_after_unmap(window->Pool, &lease);
        return 0;
    }
    *lease_out = lease;
    return 1;
}

// ====== x86_page_window_write_lease ======

int x86_page_window_write_lease(X86_PAGE_WINDOW *window, const KERNEL_PHYSICAL_LEASE *lease,
                                BOOT_U64 byte_offset, const void *src, BOOT_U64 bytes)
{
    if (!src || !bytes || !lease || !x86_page_window_idle_valid(window) ||
        !lease_frame_valid(window, lease->Base, lease->Pages) ||
        !kernel_physical_is_pinned(window->Pool, lease)) {
        return 0;
    }
    BOOT_U64 total_bytes = lease->Pages * 4096ULL;
    if (byte_offset > total_bytes || bytes > total_bytes - byte_offset) {
        return 0;
    }
    BOOT_U64 src_addr = (BOOT_U64)src;
    if (src_addr > ~0ULL - bytes ||
        os_address_ranges_overlap(src_addr, bytes, X86_KERNEL_PAGE_WINDOW, X86_KERNEL_WINDOW_BYTES)) {
        return 0;
    }
    volatile unsigned char *target = x86_window_bytes();
    volatile BOOT_U64 *entries = x86_window_entries(3);
    if (!target || !entries) {
        return 0;
    }
    const unsigned char *input = (const unsigned char *)src;
    BOOT_U64 copied = 0;
    while (copied < bytes) {
        BOOT_U64 cursor = byte_offset + copied;
        BOOT_U64 page_index = cursor >> 12;
        BOOT_U64 page_off = cursor & 4095ULL;
        BOOT_U64 amount = bytes - copied;
        if (amount > 4096ULL - page_off) {
            amount = 4096ULL - page_off;
        }
        entries[WINDOW_LEAF] = (lease->Base + page_index * 4096ULL) | 3ULL | PAGE_NX;
        x86_invalidate_page(X86_KERNEL_PAGE_WINDOW);
        for (BOOT_U64 index = 0; index < amount; ++index) {
            target[page_off + index] = input[copied + index];
        }
        entries[WINDOW_LEAF] = 0;
        x86_invalidate_page(X86_KERNEL_PAGE_WINDOW);
        copied += amount;
    }
    return leaves_match(window);
}

// ====== x86_page_window_read_lease ======

int x86_page_window_read_lease(X86_PAGE_WINDOW *window, const KERNEL_PHYSICAL_LEASE *lease,
                               BOOT_U64 byte_offset, void *dst, BOOT_U64 bytes)
{
    if (!dst || !bytes || !lease || !x86_page_window_idle_valid(window) ||
        !lease_frame_valid(window, lease->Base, lease->Pages) ||
        !kernel_physical_is_pinned(window->Pool, lease)) {
        return 0;
    }
    BOOT_U64 total_bytes = lease->Pages * 4096ULL;
    if (byte_offset > total_bytes || bytes > total_bytes - byte_offset) {
        return 0;
    }
    BOOT_U64 dst_addr = (BOOT_U64)dst;
    if (dst_addr > ~0ULL - bytes ||
        os_address_ranges_overlap(dst_addr, bytes, X86_KERNEL_PAGE_WINDOW, X86_KERNEL_WINDOW_BYTES)) {
        return 0;
    }
    const volatile unsigned char *source = x86_window_bytes();
    volatile BOOT_U64 *entries = x86_window_entries(3);
    if (!source || !entries) {
        return 0;
    }
    unsigned char *output = (unsigned char *)dst;
    BOOT_U64 copied = 0;
    while (copied < bytes) {
        BOOT_U64 cursor = byte_offset + copied;
        BOOT_U64 page_index = cursor >> 12;
        BOOT_U64 page_off = cursor & 4095ULL;
        BOOT_U64 amount = bytes - copied;
        if (amount > 4096ULL - page_off) {
            amount = 4096ULL - page_off;
        }
        entries[WINDOW_LEAF] = (lease->Base + page_index * 4096ULL) | 3ULL | PAGE_NX;
        x86_invalidate_page(X86_KERNEL_PAGE_WINDOW);
        BOOT_U64 index = 0;
        if (((page_off | copied) & 3ULL) == 0) {
            const volatile BOOT_U32 *src32 = (const volatile BOOT_U32 *)(source + page_off);
            BOOT_U32 *dst32 = (BOOT_U32 *)(output + copied);
            BOOT_U64 words = amount >> 2;
            for (BOOT_U64 w = 0; w < words; ++w) {
                dst32[w] = src32[w];
            }
            index = words << 2;
        }
        for (; index < amount; ++index) {
            output[copied + index] = source[page_off + index];
        }
        entries[WINDOW_LEAF] = 0;
        x86_invalidate_page(X86_KERNEL_PAGE_WINDOW);
        copied += amount;
    }
    return leaves_match(window);
}

// ====== x86_page_window_free_lease ======

int x86_page_window_free_lease(X86_PAGE_WINDOW *window, KERNEL_PHYSICAL_LEASE *lease)
{
    if (!lease || !x86_page_window_idle_valid(window) ||
        !lease_frame_valid(window, lease->Base, lease->Pages) ||
        !kernel_physical_release_fits(window->Pool, lease)) {
        return 0;
    }
    if (!zero_lease_chunks(window, lease)) {
        return 0;
    }
#ifdef KERNEL_AP_RUNTIME
    if (xis_native_shootdown()) x86_paging_generation_bump();
#endif
    if (!kernel_physical_release_after_unmap(window->Pool, lease)) {
        return 0;
    }
    return x86_page_window_idle_valid(window);
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
