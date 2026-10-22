#include "../activation.h"
#include <intrin.h>

// ====== allowed_private ======

// Intel VT-d5.0 11.4.4/5/7: natural-width UC scalars on the single writable unit, exact whitelist
// (GCMD commands, RTADDR root or zero, FSTS.PFO, FRCD.F); everything else is refused.
static int allowed_private(const X86_VTD_ACTIVATION *owner,BOOT_U32 offset,BOOT_U32 width,BOOT_U32 write,BOOT_U64 value)
{
    return x86_vtd_activation_valid(owner) && x86_vtd_activation_allowed(offset,width,write,value,
        owner->Root,owner->FaultOffset,owner->FaultCount);
}

// ====== x86_vtd_activation_read ======

BOOT_U64 x86_vtd_activation_read(const X86_VTD_ACTIVATION *owner,BOOT_U32 offset,BOOT_U32 width)
{
    if (!allowed_private(owner,offset,width,0,0)) return ~0ULL;
    _ReadWriteBarrier();
    BOOT_U64 value=width==4?*(const volatile BOOT_U32 *)(X86_IOMMU_VIRTUAL+offset):
        *(const volatile BOOT_U64 *)(X86_IOMMU_VIRTUAL+offset);
    _ReadWriteBarrier(); return value;
}

// ====== x86_vtd_activation_write ======

int x86_vtd_activation_write(const X86_VTD_ACTIVATION *owner,BOOT_U32 offset,BOOT_U32 width,BOOT_U64 value)
{
    if (!allowed_private(owner,offset,width,1,value)) return 0;
    _ReadWriteBarrier();
    if (width==4) *(volatile BOOT_U32 *)(X86_IOMMU_VIRTUAL+offset)=(BOOT_U32)value;
    else *(volatile BOOT_U64 *)(X86_IOMMU_VIRTUAL+offset)=value;
    _ReadWriteBarrier(); return 1;
}
