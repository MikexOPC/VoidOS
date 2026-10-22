#include "mapping.h"
#include "arch/x86/mm/window/access.h"
typedef struct { X86_VGPU_MAPPING *Owner; BOOT_U32 Index; } VGPU_WRITE;

// ====== write_private ======

static int write_private(void *opaque)
{
    const VGPU_WRITE *request=opaque;
    X86_PAGING_GPU_AUTH *auth=&request->Owner->Authority;
    volatile BOOT_U64 *page=(volatile BOOT_U64 *)x86_window_bytes();
    if (!page) return 0;
    for (BOOT_U32 slot=0; slot<512; ++slot) {
        BOOT_U64 value=0;
        if (request->Index<2 && !slot) value=auth->Tables[request->Index+1].Base|3;
        if (request->Index==2 && slot && slot<=auth->Plan.Count)
            value=auth->Plan.Physical[slot-1]|auth->Cache|0x8000000000000003ULL;
        page[slot]=value;
        if (page[slot] != value) return 0;
    }
    return 1;
}

// ====== x86_vgpu_construct ======

// Intel SDM3A 5.5.2/14.12: 4KiB supervisor RW/XD, PAT UC inherited;
// ancestors P/RW, no global bits. No device register reads or writes.
int x86_vgpu_construct(X86_VGPU_MAPPING *owner)
{
    VGPU_WRITE request={owner,0};
    for (request.Index=0; request.Index<3; ++request.Index) {
        const KERNEL_PHYSICAL_LEASE *lease=&owner->Authority.Tables[request.Index];
        for (BOOT_U32 table=0; table<owner->Inventory->Count; ++table) {
            const X86_PAGING_RESOURCE *record=&owner->Inventory->Tables[table];
            const volatile BOOT_U64 *entries;
            if (record->Physical==lease->Base) return 0;
            if (record->Level != 3) continue;
            entries=x86_paging_view(record->View);
            if (!entries) return 0;
            for (BOOT_U32 slot=0; slot<512; ++slot)
                if (entries[slot] && (entries[slot]&0x000ffffffffff000ULL)==lease->Base) return 0;
        }
        if (!kernel_physical_is_pinned(owner->Window->Pool,lease) ||
            !x86_apic_borrow(owner->Window,lease->Base,0,1,write_private,&request) ||
            !x86_page_window_idle_valid(owner->Window)) return 0;
    }
    return 1;
}
