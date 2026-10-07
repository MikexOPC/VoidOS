#include "probe.h"
#define FRAME_MASK 0x000ffffffffff000ULL

// ====== x86_hpet_probe_preflight ======

/* A validated ACPI HPET address may be absent from the UEFI memory map.
 * Permit that gap only after rejecting every RAM region, free/live lease,
 * page-table page and existing leaf alias. No hardcoded HPET base exception.
 * Initial profile requires a 4KiB-aligned base and maps exactly one page. */
int x86_hpet_probe_preflight(const KERNEL_BOOT_CONTEXT *context,
    const KERNEL_PHYSICAL_ALLOCATOR *pool, const X86_PAGING_INVENTORY *inventory,
    BOOT_U64 physical)
{
    KERNEL_MEMORY_INVENTORY map;
    BOOT_U32 index, slot, found = 0;
    if (!context || !pool || !inventory || (physical & 4095) || physical < 4096 ||
        physical > ~0ULL - 4096 ||
        !x86_paging_inventory_verify(context, pool, inventory) ||
        !x86_hpet_table_span(physical + 4096 - 4, 4, pool->PhysicalBits) ||
        !kernel_inventory_memory(context->MemoryMap, context->Info.MemoryMapBytes,
            context->Info.MemoryDescriptorBytes, &map)) { return 0; }
    for (index = 0; index < map.Regions; ++index) {
        KERNEL_MEMORY_REGION region;
        if (!kernel_read_region(context->MemoryMap, context->Info.MemoryMapBytes,
            context->Info.MemoryDescriptorBytes, index, &region)) { return 0; }
        if (!kernel_regions_overlap(physical, 1, region.Base, region.Pages)) { continue; }
        if ((region.Type != 0 && region.Type != 11) || (region.Attributes & 0x3000) ||
            (region.Attributes && !(region.Attributes & 1)) || physical < region.Base ||
            physical - region.Base > (region.Pages - 1) * 4096 || ++found != 1) { return 0; }
    }
    for (index = 0; index < pool->Count; ++index) {
        if (kernel_regions_overlap(physical, 1, pool->Free[index].Base,
            pool->Free[index].Pages)) { return 0; }
    }
    for (index = 0; index < KERNEL_PHYSICAL_LEASES; ++index) {
        if (pool->Live[index].Id && kernel_regions_overlap(physical, 1,
            pool->Live[index].Base, pool->Live[index].Pages)) { return 0; }
    }
    for (index = 0; index < inventory->Count; ++index) {
        const X86_PAGING_RESOURCE *record = &inventory->Tables[index];
        const volatile BOOT_U64 *entries;
        if (record->Physical == physical) { return 0; }
        if (record->Level != 3) { continue; }
        entries = x86_paging_view(record->View);
        if (!entries) { return 0; }
        for (slot = 0; slot < 512; ++slot) {
            if (entries[slot] && (entries[slot] & FRAME_MASK) == physical) { return 0; }
        }
    }
    return 1;
}
