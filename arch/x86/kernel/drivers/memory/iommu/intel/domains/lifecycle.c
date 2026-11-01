#include "domain.h"

// ====== x86_vtd_domain_release ======

int x86_vtd_domain_release(X86_VTD_DOMAIN *owner)
{
    if (!x86_vtd_domain_inputs(owner)) return 0;
    for (BOOT_U32 count=owner->Allocated; count; --count) {
        KERNEL_PHYSICAL_LEASE *lease=&owner->Tables[count-1];
        if (owner->Pins&(1U<<(count-1))) {
            if (!kernel_physical_pin(owner->Window->Pool,lease,0)) return 0;
            owner->Pins&=~(1U<<(count-1));
        }
        if (!kernel_physical_release(owner->Window->Pool,lease)) return 0;
        --owner->Allocated;
    }
    if (!kernel_physical_budget_matches(owner->Window->Pool, owner->Budget)) return 0;
    for (BOOT_U32 byte=0; byte<sizeof(*owner); ++byte) ((volatile unsigned char *)owner)[byte]=0;
    return 1;
}

// ====== x86_vtd_domain_prepare ======

int x86_vtd_domain_prepare(const KERNEL_BOOT_CONTEXT *context,X86_PAGE_WINDOW *window,const X86_LAPIC_MAPPING *lapic,
    X86_PAGING_INVENTORY *inventory,const X86_VTD_DOMAIN_REQUEST *request,X86_VTD_DOMAIN *owner)
{
    if (!x86_vtd_domain_preflight(context,window,lapic,inventory,request,owner)) return 0;
    owner->Self=(BOOT_U64)owner; owner->Context=context; owner->Window=window;
    owner->Lapic=lapic; owner->Inventory=inventory; owner->Request=*request;
    owner->Root=context->Info.PageTableRoot; if (!kernel_physical_budget_read(window->Pool, &owner->Budget)) return 0;
    BOOT_U64 total=0,cursor=X86_VTD_DMA_BASE+4096;
    for (BOOT_U32 index=0; index<request->Count; ++index) {
        owner->Iova[index]=cursor; total+=request->Buffers[index].Lease.Pages;
        cursor=x86_vtd_domain_next(cursor,request->Buffers[index].Lease.Pages,total);
    }
    owner->TableCount=5+(BOOT_U32)((cursor-4096-X86_VTD_DMA_BASE+0x1fffff)/0x200000);
    owner->State=1;
    for (BOOT_U32 table=0; table<owner->TableCount; ++table) {
        if (!kernel_physical_allocate(window->Pool,1,&owner->Tables[table])) goto rollback;
        ++owner->Allocated;
        if (!kernel_physical_pin(window->Pool,&owner->Tables[table],1)) goto rollback;
        owner->Pins|=1U<<table;
    }
    if (!x86_vtd_domain_pages(owner,1)) goto rollback;
    owner->State=2;
    if (!x86_vtd_domain_verify(owner)) goto rollback;
    owner->State=3;
    return 1;
rollback:
    (void)x86_vtd_domain_release(owner);
    return 0;
}

// ====== x86_vtd_domain_revoke ======

// Intel VT-d5.0 9.8: a second-stage leaf with R0=W1=0 blocks the page. Only the verified domain
// may revoke; the leaf pages are rewritten from x86_vtd_domain_expected and verified again.
// On failure the domain stays in state 1 and the caller must retain it: hardware may still read it.
int x86_vtd_domain_revoke(X86_VTD_DOMAIN *owner, BOOT_U32 index)
{
    if (!x86_vtd_domain_verify(owner) || owner->State != 3 ||
        index >= owner->Request.Count ||
        ((owner->Revoked >> index) & 1)
    )
    return 0;

    owner->State = 1;
    owner->Revoked |= 1U << index;

    if (!x86_vtd_domain_pages(owner, 1)) return 0;

    owner->State = 2;

    if (!x86_vtd_domain_verify(owner)) return 0;

    owner->State = 3;
    return 1;
}
