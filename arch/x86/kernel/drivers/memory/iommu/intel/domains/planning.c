#include "domain.h"
#include "arch/x86/configs/layout.h"
#include "lib/memory/address_ranges.h"

// ====== request_valid_private ======

static int request_valid_private(const X86_VTD_DOMAIN_REQUEST *request,const KERNEL_PHYSICAL_ALLOCATOR *pool)
{
    if (request->Segment || request->Bus>255 || request->Device>31 || request->Function>7 ||
        request->PhysicalBits<32 || request->PhysicalBits>52 || request->PhysicalBits>pool->PhysicalBits ||
        !request->Count || request->Count>X86_VTD_DMA_RANGES) return 0;
    BOOT_U64 total=0,cursor=X86_VTD_DMA_BASE+4096;
    for (BOOT_U32 index=0; index<X86_VTD_DMA_RANGES; ++index) {
        const X86_VTD_BUFFER *buffer=&request->Buffers[index];
        if (index>=request->Count) {
            for (BOOT_U32 byte=0; byte<sizeof(*buffer); ++byte) if (((const unsigned char *)buffer)[byte]) return 0;
            continue;
        }
        if (buffer->Reserved || (buffer->Access!=1 && buffer->Access!=3) || !buffer->Lease.Pages ||
            buffer->Lease.Pages>X86_VTD_DMA_PAGES || buffer->Lease.Base>=(1ULL<<request->PhysicalBits) ||
            buffer->Lease.Pages>((1ULL<<request->PhysicalBits)-buffer->Lease.Base)/4096 ||
            !kernel_physical_is_pinned(pool,&buffer->Lease)) return 0;
        total+=buffer->Lease.Pages;
        cursor=x86_vtd_domain_next(cursor,buffer->Lease.Pages,total);
        if (!cursor) return 0;
        for (BOOT_U32 other=0; other<index; ++other)
            if (buffer->Lease.Id==request->Buffers[other].Lease.Id || kernel_regions_overlap(buffer->Lease.Base,buffer->Lease.Pages,
                request->Buffers[other].Lease.Base,request->Buffers[other].Lease.Pages)) return 0;
    }
    return 1;
}

// ====== x86_vtd_domain_inputs ======

int x86_vtd_domain_inputs(const X86_VTD_DOMAIN *owner)
{
    if (!owner || owner->Self!=(BOOT_U64)owner || !owner->Context || !owner->Window || !owner->Window->Pool ||
        !owner->Inventory || owner->Inventory->Count>X86_BOOT_TABLE_LIMIT || owner->State<1 || owner->State>3 || owner->TableCount<6 || owner->TableCount>X86_VTD_DOMAIN_TABLES ||
        owner->Allocated>owner->TableCount || (owner->Pins>>owner->Allocated) || (owner->Revoked>>owner->Request.Count) ||
        owner->Root!=owner->Context->Info.PageTableRoot || owner->Root!=owner->Window->Root ||
        !x86_page_window_idle_valid(owner->Window) || !request_valid_private(&owner->Request,owner->Window->Pool)) return 0;
    for (BOOT_U32 table=0; table<owner->Allocated; ++table) {
        const KERNEL_PHYSICAL_LEASE *lease=&owner->Tables[table];
        BOOT_U32 found=0;
        if (lease->Pages!=1 || lease->Base>=(1ULL<<owner->Request.PhysicalBits)) return 0;
        for (BOOT_U32 index=0; index<KERNEL_PHYSICAL_LEASES; ++index) {
            const KERNEL_PHYSICAL_LEASE *live=&owner->Window->Pool->Live[index];
            if (live->Id==lease->Id && live->Base==lease->Base && live->Pages==lease->Pages && live->Owner==lease->Owner && lease->Id) {
                if (owner->Window->Pool->Pins[index]!=((owner->Pins>>table)&1)) return 0;
                ++found;
            }
        }
        if (found!=1) return 0;
        for (BOOT_U32 other=0; other<table; ++other)
            if (lease->Id==owner->Tables[other].Id || lease->Base==owner->Tables[other].Base) return 0;
        for (BOOT_U32 range=0; range<owner->Request.Count; ++range)
            if (kernel_regions_overlap(lease->Base,1,owner->Request.Buffers[range].Lease.Base,owner->Request.Buffers[range].Lease.Pages)) return 0;
        for (BOOT_U32 index=0; index<owner->Inventory->Count; ++index)
            if (lease->Base==owner->Inventory->Tables[index].Physical) return 0;
    }
    return owner->Lapic?x86_paging_inventory_verify_lapic(owner->Context,owner->Window->Pool,owner->Lapic,owner->Inventory):
        x86_paging_inventory_verify(owner->Context,owner->Window->Pool,owner->Inventory);
}

// ====== x86_vtd_domain_preflight ======

int x86_vtd_domain_preflight(const KERNEL_BOOT_CONTEXT *context,X86_PAGE_WINDOW *window,const X86_LAPIC_MAPPING *lapic,
    X86_PAGING_INVENTORY *inventory,const X86_VTD_DOMAIN_REQUEST *request,X86_VTD_DOMAIN *owner)
{
    if (!context || !window || !window->Pool || !inventory || !request || !owner || ((BOOT_U64)owner&7) ||
        os_address_ranges_overlap((BOOT_U64)owner,sizeof(*owner),(BOOT_U64)context,sizeof(*context)) ||
        os_address_ranges_overlap((BOOT_U64)owner,sizeof(*owner),(BOOT_U64)window,sizeof(*window)) ||
        os_address_ranges_overlap((BOOT_U64)owner,sizeof(*owner),(BOOT_U64)window->Pool,sizeof(*window->Pool)) ||
        os_address_ranges_overlap((BOOT_U64)owner,sizeof(*owner),(BOOT_U64)inventory,sizeof(*inventory)) ||
        os_address_ranges_overlap((BOOT_U64)owner,sizeof(*owner),(BOOT_U64)lapic,sizeof(*lapic)) ||
        os_address_ranges_overlap((BOOT_U64)owner,sizeof(*owner),(BOOT_U64)request,sizeof(*request)) ||
        os_address_ranges_overlap((BOOT_U64)owner,sizeof(*owner),(BOOT_U64)context->MemoryMap,context->Info.MemoryMapCapacity) ||
        os_address_ranges_overlap((BOOT_U64)owner,sizeof(*owner),X86_LAYOUT_RECURSIVE_VIRTUAL,X86_LAYOUT_SLOT_BYTES) ||
        os_address_ranges_overlap((BOOT_U64)owner,sizeof(*owner),X86_LAYOUT_DEVICES_VIRTUAL,X86_LAYOUT_DEVICES_BYTES) ||
        os_address_ranges_overlap((BOOT_U64)owner,sizeof(*owner),X86_KERNEL_PAGE_WINDOW,X86_KERNEL_WINDOW_BYTES)) return 0;
    for (BOOT_U32 byte=0; byte<sizeof(*owner); ++byte) if (((const unsigned char *)owner)[byte]) return 0;
    if (!request_valid_private(request,window->Pool) || !x86_page_window_idle_valid(window) ||
        window->Root!=context->Info.PageTableRoot ||
        !(lapic?x86_paging_inventory_verify_lapic(context,window->Pool,lapic,inventory):
            x86_paging_inventory_verify(context,window->Pool,inventory))) return 0;
    for (BOOT_U32 index=0; index<request->Count; ++index) {
        const KERNEL_PHYSICAL_LEASE *lease=&request->Buffers[index].Lease;
        if (os_address_ranges_overlap(lease->Base,lease->Pages*4096,context->Info.Framebuffer.Base,context->Info.Framebuffer.Capacity)) return 0;
        for (BOOT_U32 table=0; table<inventory->Count; ++table)
            if (kernel_regions_overlap(lease->Base,lease->Pages,inventory->Tables[table].Physical,1)) return 0;
    }
    return 1;
}
