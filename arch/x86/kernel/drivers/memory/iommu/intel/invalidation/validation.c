#include "qi.h"

// ====== x86_qi_valid ======

int x86_qi_valid(const X86_QI *owner)
{
    if (!owner || owner->Self!=(BOOT_U64)owner || !owner->Mapping || owner->Mapping->Consumer!=(BOOT_U64)owner ||
        !owner->Mapping->Context || !owner->Mapping->Window || !owner->Mapping->Window->Pool || !owner->Mapping->Inventory ||
        owner->Mapping->Inventory->Count>X86_BOOT_TABLE_LIMIT ||
        owner->Mapping->Self!=(BOOT_U64)owner->Mapping || owner->Mapping->Authority.Writable!=1 ||
        owner->Mapping->Authority.Count!=1 || owner->Allocated>2 || (owner->Pins>>owner->Allocated) ||
        owner->State<1 || owner->State>6 || owner->State==4 || owner->EnableIssued>1 || owner->Pending>1 || owner->Completed>1 || owner->Configured>1 ||
        ((owner->EnableIssued || owner->Configured) && (owner->Allocated!=2 || owner->Pins!=3)) ||
        (owner->EnableIssued && !owner->Configured) ||
        owner->PhysicalBits<32 || owner->PhysicalBits>owner->Mapping->Authority.PhysicalBits || owner->PhysicalBits>52 ||
        owner->Tail>=4096 || (owner->Tail&15) || (owner->Status&~X86_QI_FOREIGN) || owner->Clock.Ready!=1 || !owner->Clock.Port || owner->Clock.Port>65535 ||
        (owner->Clock.Mask!=0xffffff && owner->Clock.Mask!=0xffffffff) || owner->IotlbOffset>4080 ||
        !x86_vtd_mapping_check(&owner->Mapping->Authority) || x86_iommu_owner()!=&owner->Mapping->Authority) return 0;
    for (BOOT_U32 index=0; index<owner->Allocated; ++index) {
        const KERNEL_PHYSICAL_LEASE *lease=&owner->Pages[index];
        if (lease->Pages!=1 || lease->Base>=(1ULL<<owner->PhysicalBits)) return 0;
        if (!kernel_physical_lease_state(owner->Mapping->Window->Pool, lease, (owner->Pins >> index) & 1) ||
            (index && lease->Base == owner->Pages[0].Base)) return 0;
        for (BOOT_U32 table=0; table<owner->Mapping->Inventory->Count; ++table)
            if (lease->Base==owner->Mapping->Inventory->Tables[table].Physical) return 0;
    }
    return 1;
}

// ====== x86_qi_status_allow ======

// The activation owner declares which translation status bits it has set, so QI keeps polling
// QIES without accepting any other unexpected GSTS state.
int x86_qi_status_allow(X86_QI *owner,BOOT_U32 bits)
{
    if (!x86_qi_valid(owner) || (bits&~X86_QI_FOREIGN) || owner->State!=3 || owner->Pending) return 0;
    owner->Status=bits;
    return 1;
}

// ====== x86_qi_poll ======

int x86_qi_poll(X86_QI *owner,BOOT_U32 kind)
{
    BOOT_U64 initial=x86_qi_ticks(owner),previous=initial;
    BOOT_U32 stagnant=0;
    if (initial==~0ULL || kind>2) return 0;
    for (BOOT_U32 count=0; count<4096; ++count) {
        BOOT_U64 now=x86_qi_ticks(owner),raw=x86_qi_read(owner,0x1c,4),state=raw==~0ULL?raw:raw&~(BOOT_U64)owner->Status;
        if (now==~0ULL || ((now-initial)&owner->Clock.Mask)>=X86_PM_CLOCK_HZ ||
            (now==previous && ++stagnant==256) || x86_qi_read(owner,0x34,4) ||
            (state!=0 && state!=X86_QI_ENABLE)) return 0;
        if (now!=previous) stagnant=0;
        previous=now;
        if (kind==0 && state==X86_QI_ENABLE) return 1;
        if (kind==2 && state==0) return 1;
        if (kind==1) {
            BOOT_U32 status=0;
            if (state!=X86_QI_ENABLE || x86_qi_read(owner,0x90,8)!=owner->Pages[0].Base ||
                x86_qi_read(owner,0x88,8)!=owner->Tail || !x86_qi_memory(owner,2,&status)) return 0;
            if (status && status!=owner->Token) return 0;
            if (status==owner->Token && x86_qi_read(owner,0x80,8)==owner->Tail) return 1;
        }
    }
    return 0;
}
