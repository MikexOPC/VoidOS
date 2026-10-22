#include "mapping.h"
#include "lib/memory/address_ranges.h"
#include <intrin.h>

// ====== x86_vtd_capabilities_read ======

// Intel VT-d5.0 11.4.1/2/3/4.2: VER0/u32, CAP8/u64, ECAP0x10/u64, GSTS0x1c/u32 RO.
// Natural-width volatile scalar reads only; compiler barriers preserve the read order.
int x86_vtd_capabilities_read(X86_VTD_MAPPING *owner,BOOT_U32 unit,X86_VTD_CAPABILITIES *output)
{
    if (!owner || owner->Self!=(BOOT_U64)owner || !owner->Context || !owner->Window || !owner->Window->Pool ||
        !owner->Inventory || !output || ((BOOT_U64)output&7) ||
        x86_iommu_owner()!=&owner->Authority || unit>=owner->Authority.Count || unit>=8 ||
        os_address_ranges_overlap((BOOT_U64)output,sizeof(*output),(BOOT_U64)owner,sizeof(*owner)) ||
        os_address_ranges_overlap((BOOT_U64)output,sizeof(*output),(BOOT_U64)owner->Context,sizeof(*owner->Context)) ||
        os_address_ranges_overlap((BOOT_U64)output,sizeof(*output),(BOOT_U64)owner->Window,sizeof(*owner->Window)) ||
        os_address_ranges_overlap((BOOT_U64)output,sizeof(*output),(BOOT_U64)owner->Window->Pool,sizeof(*owner->Window->Pool)) ||
        os_address_ranges_overlap((BOOT_U64)output,sizeof(*output),(BOOT_U64)owner->Inventory,sizeof(*owner->Inventory)) ||
        os_address_ranges_overlap((BOOT_U64)output,sizeof(*output),X86_IOMMU_VIRTUAL-4096,17*4096) ||
        !x86_vtd_mapping_check(&owner->Authority) ||
        !x86_vtd_inventory_verify(owner)) return 0;
    BOOT_U64 address=X86_IOMMU_VIRTUAL+unit*8192ULL;
    const volatile BOOT_U32 *version=(const volatile BOOT_U32 *)x86_vtd_register_view(address,4);
    const volatile BOOT_U64 *cap=(const volatile BOOT_U64 *)x86_vtd_register_view(address+8,8);
    const volatile BOOT_U64 *ecap=(const volatile BOOT_U64 *)x86_vtd_register_view(address+0x10,8);
    const volatile BOOT_U32 *status=(const volatile BOOT_U32 *)x86_vtd_register_view(address+0x1c,4);
    if (!version || !cap || !ecap || !status) return 0;
    _ReadWriteBarrier();
    X86_VTD_CAPABILITIES draft={0};
    draft.Version=*version; draft.Cap=*cap; draft.Ecap=*ecap; draft.Status=*status; draft.Span=owner->Span[unit];
    _ReadWriteBarrier();
    if (*version!=draft.Version || *cap!=draft.Cap || *ecap!=draft.Ecap || *status!=draft.Status) return 0;
    BOOT_U32 policy=x86_vtd_capability_policy(draft.Version,draft.Cap,draft.Ecap,draft.Span,draft.Status);
    if (!policy || !x86_vtd_inventory_verify(owner)) return 0;
    draft.AddressBits=(BOOT_U32)((draft.Cap>>16)&63)+1;
    draft.Domains=(draft.Cap&7)<=6?16U<<(2*(draft.Cap&7)):0;
    draft.IotlbOffset=(BOOT_U32)((draft.Ecap>>8)&1023)*16; draft.FaultOffset=(BOOT_U32)((draft.Cap>>24)&1023)*16;
    draft.FaultCount=(BOOT_U32)((draft.Cap>>40)&255)+1; draft.Compatible=policy-1;
    *output=draft;
    return 1;
}
