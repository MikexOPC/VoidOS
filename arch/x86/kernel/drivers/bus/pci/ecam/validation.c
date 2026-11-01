#include "ecam.h"
#include "lib/memory/address_ranges.h"

// ====== x86_ecam_safe ======

int x86_ecam_safe(const X86_ECAM *owner,BOOT_U64 base,BOOT_U64 bytes)
{
    if (!owner || ((BOOT_U64)owner&7) || !owner->Context || !owner->Window || !owner->Window->Pool || !owner->Inventory ||
        owner->Info.Bits<32 || owner->Info.Bits>52) return 0;
    const KERNEL_BOOT_CONTEXT *context=owner->Context;
    const KERNEL_PHYSICAL_ALLOCATOR *pool=owner->Window->Pool;
    if (!bytes || bytes>(1ULL<<owner->Info.Bits) || base<4096 || (base&4095) || (bytes&4095) || base>(1ULL<<owner->Info.Bits)-bytes ||
        os_address_ranges_overlap(base,bytes,context->Info.Framebuffer.Base,context->Info.Framebuffer.Capacity) ||
        owner->Inventory->Count>X86_BOOT_TABLE_LIMIT) return 0;
    KERNEL_MEMORY_INVENTORY map;
    if (!kernel_inventory_memory(context->MemoryMap,context->Info.MemoryMapBytes,context->Info.MemoryDescriptorBytes,&map)) return 0;
    for (BOOT_U32 index=0; index<map.Regions; ++index) {
        KERNEL_MEMORY_REGION region;
        if (!kernel_read_region(context->MemoryMap,context->Info.MemoryMapBytes,context->Info.MemoryDescriptorBytes,index,&region)) return 0;
        if (kernel_regions_overlap(base,bytes/4096,region.Base,region.Pages) &&
            ((region.Type!=0 && region.Type!=11) || (region.Attributes&0x3000) || (region.Attributes && !(region.Attributes&1)))) return 0;
    }
    if (!kernel_physical_range_unowned(pool, base, bytes / 4096)) return 0;
    for (BOOT_U32 index=0; index<owner->Inventory->Count; ++index) {
        const X86_PAGING_RESOURCE *record=&owner->Inventory->Tables[index];
        if (os_address_ranges_overlap(base,bytes,record->Physical,4096)) return 0;
        if (record->Level!=3) continue;
        const volatile BOOT_U64 *entries=x86_paging_view(record->View);
        if (!entries) return 0;
        for (BOOT_U32 slot=0; slot<512; ++slot)
            if (entries[slot] && os_address_ranges_overlap(base,bytes,entries[slot]&0x000ffffffffff000ULL,4096)) return 0;
    }
    return 1;
}

// ====== x86_ecam_current ======

int x86_ecam_current(const X86_ECAM *owner)
{
    X86_FB_CPU cpu;
    if (!owner || ((BOOT_U64)owner&7) || owner->Self!=(BOOT_U64)owner || owner->Ready!=1 || owner->Busy || !owner->Context ||
        !owner->Window || !owner->Window->Pool || !owner->Inventory || !x86_mcfg_valid(&owner->Info) ||
        !x86_fb_cpu(&cpu) || cpu.Root!=owner->Root || cpu.Pat!=owner->Pat || cpu.PhysicalBits!=owner->Info.Bits ||
        owner->Root!=owner->Context->Info.PageTableRoot || owner->Root!=owner->Window->Root ||
        owner->Info.Bits!=owner->Window->Pool->PhysicalBits || !x86_page_window_idle_valid(owner->Window) ||
        !x86_apic_uc_slot(cpu.Pat) || owner->Cache!=x86_apic_cache_bits(x86_apic_uc_slot(cpu.Pat)-1)) return 0;
    return owner->Lapic?x86_paging_inventory_verify_lapic(owner->Context,owner->Window->Pool,owner->Lapic,owner->Inventory):
        x86_paging_inventory_verify(owner->Context,owner->Window->Pool,owner->Inventory);
}
