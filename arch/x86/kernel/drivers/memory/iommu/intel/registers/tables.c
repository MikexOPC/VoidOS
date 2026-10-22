#include "mapping.h"
#include "arch/x86/mm/window/access.h"
typedef struct { X86_VTD_MAPPING *Owner; BOOT_U32 Index; } VTD_WRITE;

// ====== write_private ======

static int write_private(void *opaque)
{
    const VTD_WRITE *request=opaque;
    const X86_IOMMU_AUTH *auth=&request->Owner->Authority;
    volatile BOOT_U64 *page=(volatile BOOT_U64 *)x86_window_bytes();
    if (!page) return 0;
    for (BOOT_U32 slot=0; slot<512; ++slot) {
        BOOT_U64 base=0,child=0;
        if (request->Index<2) child=auth->Tables[request->Index+1].Base;
        if (request->Index==2 && slot && slot<=15 && (slot&1) && slot/2<auth->Count) base=auth->Base[slot/2];
        BOOT_U64 value=x86_vtd_entry_access(request->Index,slot,base,auth->Cache,child,(BOOT_U32)auth->Writable);
        if (value==~0ULL) return 0;
        page[slot]=value;
        if (page[slot]!=value) return 0;
    }
    return 1;
}

// ====== x86_vtd_mapping_construct ======

// Intel SDM3A5.5.4/5.6: P0/RW1 ancestors; leaf RO supervisor XD63, UC via verified PAT.
int x86_vtd_mapping_construct(X86_VTD_MAPPING *owner)
{
    VTD_WRITE request={owner,0};
    for (request.Index=0; request.Index<3; ++request.Index) {
        const KERNEL_PHYSICAL_LEASE *lease=&owner->Authority.Tables[request.Index];
        for (BOOT_U32 index=0; index<owner->Inventory->Count; ++index) {
            const X86_PAGING_RESOURCE *record=&owner->Inventory->Tables[index];
            if (record->Physical==lease->Base) return 0;
            if (record->Level!=3) continue;
            const volatile BOOT_U64 *entries=x86_paging_view(record->View);
            if (!entries) return 0;
            for (BOOT_U32 slot=0; slot<512; ++slot)
                if (entries[slot] && (entries[slot]&0x000ffffffffff000ULL)==lease->Base) return 0;
        }
        if (!kernel_physical_is_pinned(owner->Window->Pool,lease) ||
            !x86_apic_borrow(owner->Window,lease->Base,0,1,write_private,&request) || !x86_page_window_idle_valid(owner->Window)) return 0;
    }
    return 1;
}
