#include "../qi.h"
#include <intrin.h>

// ====== allowed_private ======

// Intel VT-d5.0 11.4.4/6/7/9: scalar natural-width UC; exact read/write whitelist.
static int allowed_private(const X86_QI *owner,BOOT_U32 offset,BOOT_U32 width,int write)
{
    return x86_qi_valid(owner) && x86_qi_register_allowed(offset,width,(BOOT_U32)write,0,
        owner->Allocated==2 && owner->Pins==3?owner->Pages[0].Base:0,owner->IotlbOffset);
}

// ====== x86_qi_read ======

BOOT_U64 x86_qi_read(const X86_QI *owner,BOOT_U32 offset,BOOT_U32 width)
{
    if (!allowed_private(owner,offset,width,0)) return ~0ULL;
    _ReadWriteBarrier();
    BOOT_U64 value=width==4?*(const volatile BOOT_U32 *)(X86_IOMMU_VIRTUAL+offset):
        *(const volatile BOOT_U64 *)(X86_IOMMU_VIRTUAL+offset);
    _ReadWriteBarrier(); return value;
}

// ====== x86_qi_write ======

int x86_qi_write(const X86_QI *owner,BOOT_U32 offset,BOOT_U32 width,BOOT_U64 value)
{
    if (!x86_qi_valid(owner) || !x86_qi_register_allowed(offset,width,1,value,
        owner->Allocated==2 && owner->Pins==3?owner->Pages[0].Base:0,owner->IotlbOffset)) return 0;
    _ReadWriteBarrier();
    if (width==4) *(volatile BOOT_U32 *)(X86_IOMMU_VIRTUAL+offset)=(BOOT_U32)value;
    else *(volatile BOOT_U64 *)(X86_IOMMU_VIRTUAL+offset)=value;
    _ReadWriteBarrier(); return 1;
}

// ====== x86_qi_ticks ======

BOOT_U64 x86_qi_ticks(const X86_QI *owner)
{
    if (!x86_qi_valid(owner)) return ~0ULL;
    return x86_pm_read((unsigned short)owner->Clock.Port)&owner->Clock.Mask;
}
