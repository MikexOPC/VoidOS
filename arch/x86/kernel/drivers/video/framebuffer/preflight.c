#include "transaction/internal.h"
#include "arch/x86/configs/layout.h"
#include "kernel/drivers/video/framebuffer/handoff.h"
#include "lib/memory/address_ranges.h"
#include "arch/x86/kernel/exceptions/terminal.h"

// ====== owner_range ======

static int owner_range(const X86_FB_MAPPING *owner)
{
    BOOT_U64 address = (BOOT_U64)owner;
    return owner && !(address & 7) && address <= ~0ULL - sizeof(*owner) &&
        !os_address_ranges_overlap(address, sizeof(*owner), X86_FB_VIRTUAL - 4096, 1ULL << 30) &&
        !os_address_ranges_overlap(address, sizeof(*owner), X86_LAYOUT_RECURSIVE_VIRTUAL,X86_LAYOUT_SLOT_BYTES) &&
        !os_address_ranges_overlap(address, sizeof(*owner), X86_KERNEL_PAGE_WINDOW, X86_KERNEL_WINDOW_BYTES);
}

// ====== disjoint ======

static int disjoint(const X86_FB_MAPPING *owner, const void *input, BOOT_U64 bytes)
{
    return input && !os_address_ranges_overlap((BOOT_U64)owner, sizeof(*owner), (BOOT_U64)input, bytes);
}

// ====== aperture_aliases_absent ======

static int aperture_aliases_absent(const X86_PAGING_INVENTORY *inventory,
    BOOT_U64 physical, BOOT_U64 pages)
{
    BOOT_U32 index, slot;
    for (index = 0; index < inventory->Count; ++index) {
        const X86_PAGING_RESOURCE *table = &inventory->Tables[index];
        const volatile BOOT_U64 *entries;
        if (kernel_regions_overlap(physical, pages, table->Physical, 1)) return 0;
        if (table->Level != 3) continue;
        entries = x86_paging_view(table->View);
        if (!entries) return 0;
        for (slot = 0; slot < 512; ++slot)
            if (entries[slot] && kernel_regions_overlap(physical, pages,
                entries[slot] & 0x000ffffffffff000ULL, 1)) return 0;
    }
    return 1;
}

// ====== x86_fb_preflight ======

int x86_fb_preflight(const KERNEL_BOOT_CONTEXT *context, X86_PAGE_WINDOW *window,
    const X86_LAPIC_MAPPING *lapic, const X86_PAGING_INVENTORY *inventory,
    X86_FB_MAPPING *owner, FB_MAPPING_GEOMETRY *geometry, X86_FB_CPU *cpu)
{
    BOOT_U32 byte, index;
    BOOT_U64 aperture;
    volatile BOOT_U64 *root;
    if (!owner_range(owner) || !context || !window || !window->Pool || !lapic || !inventory ||
        !disjoint(owner, context, sizeof(*context)) || !disjoint(owner, window, sizeof(*window)) ||
        !disjoint(owner, window->Pool, sizeof(*window->Pool)) || !disjoint(owner, lapic, sizeof(*lapic)) ||
        !disjoint(owner, inventory, sizeof(*inventory)) || context->Info.MemoryMapCapacity > 1048576 ||
        !disjoint(owner, context->MemoryMap, context->Info.MemoryMapCapacity)) return 0;
    for (byte = 0; byte < sizeof(*owner); ++byte)
        if (((const unsigned char *)owner)[byte]) return 0;
    if (!x86_exception_environment() || !x86_fb_cpu(cpu) || cpu->Root != window->Root || cpu->Root != context->Info.PageTableRoot ||
        cpu->PhysicalBits != window->Pool->PhysicalBits || !x86_page_window_idle_valid(window) ||
        !x86_paging_inventory_verify_lapic(context, window->Pool, lapic, inventory) ||
        !fb_handoff_memory_valid(&context->Info.Framebuffer, context->MemoryMap,
            context->Info.MemoryMapBytes, context->Info.MemoryDescriptorBytes, cpu->PhysicalBits) ||
        !fb_mapping_geometry(&context->Info.Framebuffer, cpu->PhysicalBits, geometry)) return 0;
    root = x86_fb_root();
    if (!root || root[X86_FB_SLOT]) return 0;
    if (!geometry->Pages) return 1;
    aperture = (geometry->Offset + context->Info.Framebuffer.Capacity + 4095) / 4096;
    index = x86_apic_uc_slot(cpu->Pat);
    return index && kernel_physical_budget_at_least(window->Pool, geometry->TablePages) &&
        x86_paging_video_policy(X86_FB_VIRTUAL, geometry->Physical, geometry->Pages,
            aperture, x86_apic_cache_bits(index - 1), cpu->Pat, cpu->PhysicalBits) &&
        aperture_aliases_absent(inventory, geometry->Physical, aperture);
}
