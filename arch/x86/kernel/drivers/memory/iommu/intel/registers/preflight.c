#include "mapping.h"
#include "arch/x86/configs/layout.h"
#include "lib/memory/address_ranges.h"

// ====== x86_vtd_mapping_preflight ======

int x86_vtd_mapping_preflight(const KERNEL_BOOT_CONTEXT *context,X86_PAGE_WINDOW *window,const X86_LAPIC_MAPPING *lapic,const X86_DMAR_INFO *description,
    X86_PAGING_INVENTORY *inventory,X86_VTD_MAPPING *owner,X86_FB_CPU *cpu)
{
    if (!context || !window || !window->Pool || !description || !inventory || !owner || !cpu ||
        ((BOOT_U64)owner&7) || x86_iommu_owner() || description->Rmrr>description->OtherRecords || !description->Units || description->Units>8 ||
        os_address_ranges_overlap((BOOT_U64)owner,sizeof(*owner),(BOOT_U64)context,sizeof(*context)) ||
        os_address_ranges_overlap((BOOT_U64)owner,sizeof(*owner),(BOOT_U64)window,sizeof(*window)) ||
        os_address_ranges_overlap((BOOT_U64)owner,sizeof(*owner),(BOOT_U64)window->Pool,sizeof(*window->Pool)) ||
        os_address_ranges_overlap((BOOT_U64)owner,sizeof(*owner),(BOOT_U64)lapic,sizeof(*lapic)) ||
        os_address_ranges_overlap((BOOT_U64)owner,sizeof(*owner),(BOOT_U64)inventory,sizeof(*inventory)) ||
        os_address_ranges_overlap((BOOT_U64)owner,sizeof(*owner),(BOOT_U64)description,sizeof(*description)) ||
        os_address_ranges_overlap((BOOT_U64)owner,sizeof(*owner),(BOOT_U64)context->MemoryMap,context->Info.MemoryMapCapacity) ||
        os_address_ranges_overlap((BOOT_U64)owner,sizeof(*owner),X86_LAYOUT_RECURSIVE_VIRTUAL,X86_LAYOUT_SLOT_BYTES) ||
        os_address_ranges_overlap((BOOT_U64)owner,sizeof(*owner),X86_LAYOUT_DEVICES_VIRTUAL,X86_LAYOUT_DEVICES_BYTES) ||
        os_address_ranges_overlap((BOOT_U64)owner,sizeof(*owner),X86_KERNEL_PAGE_WINDOW,X86_KERNEL_WINDOW_BYTES) ||
        os_address_ranges_overlap((BOOT_U64)owner,sizeof(*owner),X86_IOMMU_VIRTUAL-4096,X86_LAYOUT_SLOT_BYTES)) return 0;
    for (BOOT_U32 byte=0; byte<sizeof(*owner); ++byte) if (((const unsigned char *)owner)[byte]) return 0;
    if (!x86_page_window_idle_valid(window) || !x86_fb_cpu(cpu) || cpu->Root!=window->Root ||
        cpu->Root!=context->Info.PageTableRoot || cpu->PhysicalBits!=window->Pool->PhysicalBits || cpu->PhysicalBits<32 || cpu->PhysicalBits>52 ||
        (cpu->Pat&255)!=6 || !x86_apic_uc_slot(cpu->Pat) || !kernel_physical_budget_at_least(window->Pool, 3) ||
        !x86_fb_root() || x86_fb_root()[X86_IOMMU_SLOT] ||
        !(lapic?x86_paging_inventory_capture_lapic(context,window->Pool,lapic,inventory):
            x86_paging_inventory_capture(context,window->Pool,inventory))) return 0;
    for (BOOT_U32 index=0; index<description->Units; ++index) {
        const X86_DMAR_UNIT *unit=&description->Unit[index];
        if (unit->Bytes<4096 || unit->Bytes>134217728 || (unit->Bytes&(unit->Bytes-1)) ||
            (unit->Base&(unit->Bytes-1)) || unit->Base>(1ULL<<cpu->PhysicalBits)-unit->Bytes ||
            !x86_iommu_page_safe(context,window->Pool,unit->Base)) return 0;
        for (BOOT_U32 other=0; other<index; ++other)
            if (os_address_ranges_overlap(unit->Base,unit->Bytes,description->Unit[other].Base,description->Unit[other].Bytes)) return 0;
        for (BOOT_U32 table=0; table<inventory->Count; ++table) {
            const X86_PAGING_RESOURCE *record=&inventory->Tables[table];
            if (record->Physical==unit->Base) return 0;
            if (record->Level!=3) continue;
            const volatile BOOT_U64 *entries=x86_paging_view(record->View);
            if (!entries) return 0;
            for (BOOT_U32 slot=0; slot<512; ++slot)
                if (entries[slot] && (entries[slot]&0x000ffffffffff000ULL)==unit->Base) return 0;
        }
    }
    return 1;
}
