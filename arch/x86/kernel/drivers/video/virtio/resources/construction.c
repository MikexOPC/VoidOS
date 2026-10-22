#include "mapping.h"
#include "arch/x86/mm/window/access.h"
typedef struct { X86_SURFACE_MAPPING *Owner; BOOT_U32 Index; } SURFACE_WRITE;

// ====== write_private ======

static int write_private(void *opaque)
{
    const SURFACE_WRITE *request=opaque;
    const X86_SURFACE_AUTH *auth=&request->Owner->Authority;
    volatile BOOT_U64 *view=(volatile BOOT_U64 *)x86_window_bytes();
    if (!view) return 0;
    for (BOOT_U32 slot=0; slot<512; ++slot) {
        BOOT_U64 child=0;
        if (!request->Index && !slot) child=auth->Tables[1].Base;
        if (request->Index==1 && slot<auth->Count-2) child=auth->Tables[slot+2].Base;
        BOOT_U64 entry=kernel_vgpu_surface_entry(request->Index,slot,auth->Data.Base,auth->Data.Pages,child);
        if (entry==~0ULL) return 0;
        view[slot]=entry;
        if (view[slot]!=entry) return 0;
    }
    return 1;
}

// ====== x86_vgpu_surface_construct ======

// Intel SDM3A5.5.4/5.6: four-level4KiB PTE, P0/RW1/US2=0/XD63; no global pages.
int x86_vgpu_surface_construct(X86_SURFACE_MAPPING *owner)
{
    SURFACE_WRITE request={owner,0};
    for (BOOT_U32 index=0; index<owner->Gpu->Inventory->Count; ++index) {
        const X86_PAGING_RESOURCE *record=&owner->Gpu->Inventory->Tables[index];
        if (kernel_regions_overlap(record->Physical,1,owner->Authority.Data.Base,owner->Authority.Data.Pages)) return 0;
        if (record->Level!=3) continue;
        const volatile BOOT_U64 *entries=x86_paging_view(record->View);
        if (!entries) return 0;
        for (BOOT_U32 slot=0; slot<512; ++slot)
            if (entries[slot] && kernel_regions_overlap(entries[slot]&0x000ffffffffff000ULL,1,
                owner->Authority.Data.Base,owner->Authority.Data.Pages)) return 0;
    }
    for (request.Index=0; request.Index<owner->Authority.Count; ++request.Index) {
        const KERNEL_PHYSICAL_LEASE *lease=&owner->Authority.Tables[request.Index];
        for (BOOT_U32 table=0; table<owner->Gpu->Inventory->Count; ++table) {
            const X86_PAGING_RESOURCE *record=&owner->Gpu->Inventory->Tables[table];
            const volatile BOOT_U64 *entries;
            if (record->Physical==lease->Base) return 0;
            if (record->Level!=3) continue;
            entries=x86_paging_view(record->View);
            if (!entries) return 0;
            for (BOOT_U32 slot=0; slot<512; ++slot)
                if (entries[slot] && (entries[slot]&0x000ffffffffff000ULL)==lease->Base) return 0;
        }
        if (!kernel_physical_is_pinned(owner->Gpu->Window->Pool,lease) ||
            !x86_apic_borrow(owner->Gpu->Window,lease->Base,0,1,write_private,&request) ||
            !x86_page_window_idle_valid(owner->Gpu->Window)) return 0;
    }
    return 1;
}
