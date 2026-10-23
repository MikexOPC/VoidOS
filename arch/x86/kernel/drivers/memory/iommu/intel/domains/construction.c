#include "domain.h"
#include "arch/x86/mm/window/access.h"
typedef struct { X86_VTD_DOMAIN *Owner; BOOT_U32 Table; int Write; } DOMAIN_IO;

// ====== x86_vtd_domain_expected ======

// Intel VT-d5.0 9.1/9.3/9.8: root/context P0, DID[87:72]=1, AW[66:64]=2,
// TT[3:2]=0, FPD1=0; second-stage R0/W1, all unused/reserved fields zero.
BOOT_U64 x86_vtd_domain_expected(const X86_VTD_DOMAIN *owner,BOOT_U32 table,BOOT_U32 slot)
{
    if (!owner || owner->TableCount>X86_VTD_DOMAIN_TABLES || table>=owner->TableCount || slot>=512 ||
        owner->Request.Count>X86_VTD_DMA_RANGES) return ~0ULL;
    BOOT_U64 child=0,physical=0; BOOT_U32 access=0,selector=0,kind=table;
    if (table==0) { selector=owner->Request.Bus; child=owner->Tables[1].Base; }
    if (table==1) { selector=owner->Request.Device*8+owner->Request.Function; child=owner->Tables[2].Base; }
    if (table==2 || table==3) child=owner->Tables[table+1].Base;
    if (table==4 && slot>=8 && slot<8+owner->TableCount-5) child=owner->Tables[5+slot-8].Base;
    if (table>=5) {
        kind=5;
        BOOT_U64 iova=X86_VTD_DMA_BASE+(table-5)*0x200000ULL+slot*4096ULL;
        for (BOOT_U32 index=0; index<owner->Request.Count; ++index) {
            const X86_VTD_BUFFER *buffer=&owner->Request.Buffers[index];
            if (((owner->Revoked>>index)&1)==0 && iova>=owner->Iova[index] && iova-owner->Iova[index]<buffer->Lease.Pages*4096) {
                physical=buffer->Lease.Base+iova-owner->Iova[index]; access=buffer->Access;
            }
        }
    }
    return x86_vtd_domain_word(kind,slot,selector,child,physical,access,owner->Request.PhysicalBits);
}

// ====== page_private ======

static int page_private(void *opaque)
{
    const DOMAIN_IO *io=opaque;
    volatile BOOT_U64 *page=(volatile BOOT_U64 *)x86_window_bytes();
    if (!page) return 0;
    for (BOOT_U32 slot=0; slot<512; ++slot) {
        BOOT_U64 expected=x86_vtd_domain_expected(io->Owner,io->Table,slot);
        if (expected==~0ULL) return 0;
        if (io->Write) page[slot]=expected;
        if (page[slot]!=expected) return 0;
    }
    return 1;
}

// ====== x86_vtd_domain_pages ======

int x86_vtd_domain_pages(X86_VTD_DOMAIN *owner,int write)
{
    if (!x86_vtd_domain_inputs(owner) || (write!=0 && write!=1) || owner->Allocated!=owner->TableCount ||
        owner->Pins!=((1U<<owner->TableCount)-1) || (write && owner->State!=1)) return 0;
    for (BOOT_U32 table=0; table<owner->TableCount; ++table) {
        const KERNEL_PHYSICAL_LEASE *lease=&owner->Tables[table];
        if (lease->Pages!=1 || !kernel_physical_is_pinned(owner->Window->Pool,lease)) return 0;
        if (lease->Base>=(1ULL<<owner->Request.PhysicalBits)) return 0;
        for (BOOT_U32 other=0; other<table; ++other)
            if (lease->Id==owner->Tables[other].Id || lease->Base==owner->Tables[other].Base) return 0;
        for (BOOT_U32 range=0; range<owner->Request.Count; ++range)
            if (kernel_regions_overlap(lease->Base,1,owner->Request.Buffers[range].Lease.Base,owner->Request.Buffers[range].Lease.Pages)) return 0;
        for (BOOT_U32 index=0; index<owner->Inventory->Count; ++index)
            if (lease->Base==owner->Inventory->Tables[index].Physical) return 0;
    }
    for (BOOT_U32 table=0; table<owner->TableCount; ++table) {
        const KERNEL_PHYSICAL_LEASE *lease=&owner->Tables[table];
        DOMAIN_IO io={owner,table,write};
        if (!x86_apic_borrow(owner->Window,lease->Base,0,write,page_private,&io) || !x86_page_window_idle_valid(owner->Window)) return 0;
    }
    return x86_vtd_domain_inputs(owner);
}
