#include "mapping.h"
#include "arch/x86/configs/layout.h"
#include "arch/x86/mm/window/access.h"
#include "lib/memory/address_ranges.h"

// ====== x86_vgpu_preflight ======

int x86_vgpu_preflight(const KERNEL_BOOT_CONTEXT *context, X86_PAGE_WINDOW *window,
    const X86_LAPIC_MAPPING *lapic, X86_PAGING_INVENTORY *inventory,
    X86_VGPU_CAPTURE capture, X86_VGPU_MAPPING *owner, X86_FB_CPU *cpu, KERNEL_VGPU_PAGE_PLAN *plan)
{
    BOOT_U64 address=(BOOT_U64)owner;
    if (!owner || (address & 7) || address>~0ULL-sizeof(*owner) || !context || !window || !window->Pool ||
        !lapic || !inventory || !capture || !cpu || !plan || x86_paging_gpu_owner() ||
        context->Info.MemoryMapCapacity>1048576 ||
        os_address_ranges_overlap(address,sizeof(*owner),(BOOT_U64)context,sizeof(*context)) ||
        os_address_ranges_overlap(address,sizeof(*owner),(BOOT_U64)window,sizeof(*window)) ||
        os_address_ranges_overlap(address,sizeof(*owner),(BOOT_U64)window->Pool,sizeof(*window->Pool)) ||
        os_address_ranges_overlap(address,sizeof(*owner),(BOOT_U64)lapic,sizeof(*lapic)) ||
        os_address_ranges_overlap(address,sizeof(*owner),(BOOT_U64)inventory,sizeof(*inventory)) ||
        os_address_ranges_overlap(address,sizeof(*owner),(BOOT_U64)context->MemoryMap,context->Info.MemoryMapCapacity) ||
        os_address_ranges_overlap(address,sizeof(*owner),X86_LAYOUT_RECURSIVE_VIRTUAL,X86_LAYOUT_SLOT_BYTES) ||
        os_address_ranges_overlap(address,sizeof(*owner),X86_GPU_VIRTUAL-4096,X86_LAYOUT_SLOT_BYTES) ||
        os_address_ranges_overlap(address,sizeof(*owner),X86_FB_VIRTUAL-4096,X86_LAYOUT_SLOT_BYTES) ||
        os_address_ranges_overlap(address,sizeof(*owner),X86_LAPIC_VIRTUAL-4096,12288) ||
        os_address_ranges_overlap(address,sizeof(*owner),X86_KERNEL_PAGE_WINDOW,X86_KERNEL_WINDOW_BYTES)) return 0;
    for (BOOT_U32 byte=0; byte<sizeof(*owner); ++byte)
        if (((const unsigned char *)owner)[byte]) return 0;
    if (!x86_fb_cpu(cpu) || cpu->Root != window->Root || cpu->Root != context->Info.PageTableRoot ||
        cpu->PhysicalBits != window->Pool->PhysicalBits || !x86_page_window_idle_valid(window) ||
        !capture(context,window->Pool,lapic,inventory) ||
        !kernel_vgpu_plan(&context->Info.Gpu,cpu->PhysicalBits,plan)) return 0;
    if (!plan->Count) return 1;
    if ((cpu->Pat & 255) != 6 || !x86_apic_uc_slot(cpu->Pat) || window->Pool->FreePages<3 ||
        !x86_vgpu_pci_match(&context->Info.Gpu,cpu->PhysicalBits) || !x86_fb_root() || x86_fb_root()[X86_GPU_SLOT]) return 0;
    for (BOOT_U32 page=0; page<plan->Count; ++page) {
        BOOT_U64 physical=plan->Physical[page];
        if (os_address_ranges_overlap(physical,4096,context->Info.Framebuffer.Base,
            context->Info.Framebuffer.Capacity)) return 0;
        for (BOOT_U32 index=0; index<inventory->Count; ++index) {
            const X86_PAGING_RESOURCE *table=&inventory->Tables[index];
            const volatile BOOT_U64 *entries;
            if (table->Physical == physical) return 0;
            if (table->Level != 3) continue;
            entries=x86_paging_view(table->View);
            if (!entries) return 0;
            for (BOOT_U32 slot=0; slot<512; ++slot)
                if (entries[slot] && (entries[slot] & 0x000ffffffffff000ULL)==physical) return 0;
        }
        for (BOOT_U64 index=0; index<context->Info.MemoryMapBytes/context->Info.MemoryDescriptorBytes; ++index) {
            KERNEL_MEMORY_REGION region;
            if (!kernel_read_region(context->MemoryMap,context->Info.MemoryMapBytes,
                context->Info.MemoryDescriptorBytes,index,&region)) return 0;
            if (kernel_regions_overlap(physical,1,region.Base,region.Pages) &&
                ((region.Type != 0 && region.Type != 11) || (region.Attributes & 0x3000))) return 0;
        }
        for (BOOT_U32 index=0; index<window->Pool->Count; ++index)
            if (kernel_regions_overlap(physical,1,window->Pool->Free[index].Base,window->Pool->Free[index].Pages)) return 0;
        for (BOOT_U32 index=0; index<KERNEL_PHYSICAL_LEASES; ++index)
            if (window->Pool->Live[index].Id && kernel_regions_overlap(physical,1,
                window->Pool->Live[index].Base,window->Pool->Live[index].Pages)) return 0;
    }
    return 1;
}
