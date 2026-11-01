#include "mapping.h"

// ====== x86_vtd_inventory_capture ======

int x86_vtd_inventory_capture(const X86_VTD_MAPPING *owner)
{
    return owner->Lapic?x86_paging_inventory_capture_lapic(owner->Context,owner->Window->Pool,owner->Lapic,owner->Inventory):
        x86_paging_inventory_capture(owner->Context,owner->Window->Pool,owner->Inventory);
}

// ====== x86_vtd_inventory_verify ======

int x86_vtd_inventory_verify(const X86_VTD_MAPPING *owner)
{
    return owner->Lapic?x86_paging_inventory_verify_lapic(owner->Context,owner->Window->Pool,owner->Lapic,owner->Inventory):
        x86_paging_inventory_verify(owner->Context,owner->Window->Pool,owner->Inventory);
}

// ====== x86_vtd_mapping_check ======

int x86_vtd_mapping_check(const X86_IOMMU_AUTH *authority)
{
    X86_FB_CPU cpu;
    volatile BOOT_U64 *root=x86_fb_root();
    return authority && authority->Self==(BOOT_U64)authority && authority->Ready==1 &&
        x86_fb_cpu(&cpu) && cpu.Root==authority->Root && cpu.Pat==authority->Pat &&
        cpu.PhysicalBits==authority->PhysicalBits && root &&
        (root[X86_IOMMU_SLOT]&~0x60ULL)==(authority->Tables[0].Base|3);
}

// ====== x86_vtd_mapping_unbind ======

int x86_vtd_mapping_unbind(X86_VTD_MAPPING *owner)
{
    X86_FB_CPU cpu;
    if (!owner || owner->Self!=(BOOT_U64)owner || owner->Consumer || !owner->Context || !owner->Window || !owner->Window->Pool || !owner->Inventory ||
        owner->Allocated>3 || (owner->Pinned>>owner->Allocated) || owner->Published>1 || owner->Registered>1 ||
        !x86_fb_cpu(&cpu) || cpu.Root!=owner->Authority.Root || cpu.Pat!=owner->Authority.Pat ||
        cpu.PhysicalBits!=owner->Authority.PhysicalBits || !x86_page_window_idle_valid(owner->Window)) return 0;
    volatile BOOT_U64 *root=x86_fb_root();
    if (!root) return 0;
    if (owner->Published) {
        if (root[X86_IOMMU_SLOT] && (root[X86_IOMMU_SLOT]&~0x60ULL)!=(owner->Authority.Tables[0].Base|3)) return 0;
        root[X86_IOMMU_SLOT]=0;
        if (!x86_fb_flush(cpu.Root) || root[X86_IOMMU_SLOT]) return 0;
        owner->Published=0;
    } else if (root[X86_IOMMU_SLOT]) return 0;
    if (owner->Registered) {
        if (!x86_iommu_register(&owner->Authority,0,0)) return 0;
        owner->Registered=0;
    } else if (x86_iommu_owner()) return 0;
    owner->Authority.Ready=0;
    if (!x86_vtd_inventory_capture(owner)) return 0;
    for (BOOT_U32 count=owner->Allocated; count; --count) {
        KERNEL_PHYSICAL_LEASE *lease=&owner->Authority.Tables[count-1];
        if (owner->Pinned&(1U<<(count-1))) {
            if (!kernel_physical_pin(owner->Window->Pool,lease,0)) return 0;
            owner->Pinned&=~(1U<<(count-1));
        }
        if (!kernel_physical_release(owner->Window->Pool,lease)) return 0;
        --owner->Allocated;
    }
    if (!kernel_physical_budget_matches(owner->Window->Pool, owner->Budget)) return 0;
    for (BOOT_U32 byte=0; byte<sizeof(*owner); ++byte) ((volatile unsigned char *)owner)[byte]=0;
    return 1;
}

// ====== x86_vtd_mapping_bind ======

int x86_vtd_mapping_bind_access(const KERNEL_BOOT_CONTEXT *context,X86_PAGE_WINDOW *window,const X86_LAPIC_MAPPING *lapic,const X86_DMAR_INFO *description,
    X86_PAGING_INVENTORY *inventory,X86_VTD_MAPPING *owner,BOOT_U32 writable)
{
    X86_FB_CPU cpu;
    if (writable>1 || (writable && (!description || description->Units!=1))) return 0;
    if (!x86_vtd_mapping_preflight(context,window,lapic,description,inventory,owner,&cpu)) return 0;
    owner->Self=(BOOT_U64)owner; if (!kernel_physical_budget_read(window->Pool, &owner->Budget)) return 0;
    owner->Context=context; owner->Window=window; owner->Inventory=inventory; owner->Lapic=lapic;
    owner->Authority.Self=(BOOT_U64)&owner->Authority; owner->Authority.Root=cpu.Root;
    owner->Authority.Pat=cpu.Pat; owner->Authority.PhysicalBits=cpu.PhysicalBits; owner->Authority.Count=description->Units;
    owner->Authority.Writable=writable;
    owner->Authority.Cache=x86_apic_cache_bits(x86_apic_uc_slot(cpu.Pat)-1);
    for (BOOT_U32 index=0; index<description->Units; ++index) {
        owner->Authority.Base[index]=description->Unit[index].Base; owner->Span[index]=description->Unit[index].Bytes;
    }
    for (BOOT_U32 index=0; index<3; ++index) {
        if (!kernel_physical_allocate(window->Pool,1,&owner->Authority.Tables[index])) goto rollback;
        ++owner->Allocated;
        if (!kernel_physical_pin(window->Pool,&owner->Authority.Tables[index],1)) goto rollback;
        owner->Pinned|=1U<<index;
    }
    if (!x86_vtd_mapping_construct(owner) || x86_fb_root()[X86_IOMMU_SLOT]) goto rollback;
    owner->Published=1; x86_fb_root()[X86_IOMMU_SLOT]=owner->Authority.Tables[0].Base|3;
    if (!x86_fb_flush(cpu.Root)) goto rollback;
    owner->Authority.Ready=1;
    if (!x86_iommu_register(0,&owner->Authority,x86_vtd_mapping_check)) goto rollback;
    owner->Registered=1;
    if (!x86_vtd_inventory_capture(owner)) goto rollback;
    return 1;
rollback:
    (void)x86_vtd_mapping_unbind(owner);
    return 0;
}

// ====== x86_vtd_mapping_bind ======

int x86_vtd_mapping_bind(const KERNEL_BOOT_CONTEXT *context,X86_PAGE_WINDOW *window,const X86_LAPIC_MAPPING *lapic,const X86_DMAR_INFO *description,
    X86_PAGING_INVENTORY *inventory,X86_VTD_MAPPING *owner)
{
    return x86_vtd_mapping_bind_access(context,window,lapic,description,inventory,owner,0);
}
