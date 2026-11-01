#include "domain.h"

// ====== x86_vtd_domain_verify ======

int x86_vtd_domain_verify(X86_VTD_DOMAIN *owner)
{
    if (!x86_vtd_domain_inputs(owner) || owner->State<2) return 0;
    BOOT_U64 cursor=X86_VTD_DMA_BASE+4096,total=0;
    for (BOOT_U32 index=0; index<X86_VTD_DMA_RANGES; ++index) {
        if (index>=owner->Request.Count) { if (owner->Iova[index]) return 0; continue; }
        if (owner->Iova[index]!=cursor) return 0;
        total+=owner->Request.Buffers[index].Lease.Pages;
        cursor=x86_vtd_domain_next(cursor,owner->Request.Buffers[index].Lease.Pages,total);
        if (!cursor) return 0;
    }
    if (owner->TableCount!=5+(cursor-4096-X86_VTD_DMA_BASE+0x1fffff)/0x200000 ||
        owner->Budget<owner->Allocated || !kernel_physical_budget_matches(owner->Window->Pool, owner->Budget-owner->Allocated)) return 0;
    return x86_vtd_domain_pages(owner,0);
}
