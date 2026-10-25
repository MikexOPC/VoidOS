#include "authority.h"
#include "arch/x86/configs/layout.h"
#include "lib/memory/address_ranges.h"
static const X86_SURFACE_AUTH *registered;
static X86_SURFACE_CHECK registered_check;

// ====== x86_surface_owner ======

const X86_SURFACE_AUTH *x86_surface_owner(void) { return registered; }

// ====== x86_surface_register ======

int x86_surface_register(const X86_SURFACE_AUTH *expected,const X86_SURFACE_AUTH *next,X86_SURFACE_CHECK check)
{
    if (registered!=expected || (!next && check) ||
        (next && (expected || !check || next->Self!=(BOOT_U64)next || next->Ready!=1 || !check(next)))) return 0;
    registered_check=check; registered=next;
    return 1;
}

// ====== x86_surface_inputs ======

int x86_surface_inputs(const KERNEL_PHYSICAL_ALLOCATOR *pool,BOOT_U64 root,BOOT_U64 output,
    BOOT_U64 bytes,X86_SURFACE_SNAPSHOT *snapshot)
{
    const X86_SURFACE_AUTH *owner=registered;
    if (!snapshot) return 0;
    *snapshot=(X86_SURFACE_SNAPSHOT){0};
    if (!owner) return 1;
    if (owner->Ready!=1 || !owner->Data.Pages || owner->Data.Pages>4096) return 0;
    if (!pool || owner->Self!=(BOOT_U64)owner || owner->Ready!=1 || owner->Reserved ||
        owner->Root!=root || owner->PhysicalBits!=pool->PhysicalBits || (owner->Pat&255)!=6 ||
        !owner->Bytes || owner->Bytes>16ULL*1024*1024 || owner->Data.Pages!=(owner->Bytes+4095)/4096 ||
        owner->Count!=2+(owner->Data.Pages+2+511)/512 || owner->Count>11 ||
        !kernel_physical_is_pinned(pool,&owner->Data) || !registered_check || !registered_check(owner) ||
        os_address_ranges_overlap(output,bytes,(BOOT_U64)owner,sizeof(*owner)) ||
        os_address_ranges_overlap(output,bytes,X86_SURFACE_VIRTUAL-4096,(owner->Data.Pages+2)*4096)) return 0;
    snapshot->Owner=owner->Self; snapshot->Base=owner->Data.Base;
    snapshot->Pages=owner->Data.Pages; snapshot->DataId=owner->Data.Id;
    for (BOOT_U32 index=0; index<11; ++index) {
        const KERNEL_PHYSICAL_LEASE *lease=&owner->Tables[index];
        if (index>=owner->Count) { if (lease->Id || lease->Base || lease->Pages || lease->Owner) return 0; continue; }
        if (lease->Pages!=1 || !kernel_physical_is_pinned(pool,lease) ||
            kernel_regions_overlap(lease->Base,1,owner->Data.Base,owner->Data.Pages)) return 0;
        for (BOOT_U32 other=0; other<index; ++other)
            if (lease->Base==owner->Tables[other].Base || lease->Id==owner->Tables[other].Id) return 0;
        snapshot->TableIds[index]=lease->Id;
    }
    return 1;
}

// ====== x86_surface_source ======

BOOT_U32 x86_surface_source(const KERNEL_PHYSICAL_ALLOCATOR *pool,BOOT_U64 physical)
{
    if (registered) for (BOOT_U32 index=0; index<registered->Count && index<11; ++index)
        if (registered->Tables[index].Base==physical)
            return kernel_physical_is_pinned(pool,&registered->Tables[index])?21+index:0;
    return 0;
}

// ====== x86_surface_leaf ======

// Intel SDM3A5.5.4/5.6: P0,RW1,US2=0,XD63; WB via PAT entry0, no global bit.
BOOT_U32 x86_surface_leaf(BOOT_U64 entry,BOOT_U64 address)
{
    const X86_SURFACE_AUTH *owner=registered;
    if (!owner) return 1;
    if (X86_LAYOUT_SLOT_OF(address)==X86_SURFACE_SLOT) {
        if (address<X86_SURFACE_VIRTUAL || (address&4095)) return 0;
        BOOT_U64 index=(address-X86_SURFACE_VIRTUAL)/4096;
        return index<owner->Data.Pages && (entry&~0x60ULL)==
            ((owner->Data.Base+index*4096)|0x8000000000000003ULL)?6:0;
    }
    return kernel_regions_overlap(entry&0x000ffffffffff000ULL,1,owner->Data.Base,owner->Data.Pages)?0:1;
}

// ====== x86_surface_record ======

int x86_surface_record(const X86_SURFACE_SNAPSHOT *snapshot,BOOT_U64 physical,BOOT_U64 view,BOOT_U32 level,BOOT_U32 tag)
{
    const X86_SURFACE_AUTH *owner=registered;
    if (!owner || !snapshot || snapshot->Owner!=owner->Self || tag<20 || tag>=20+owner->Count || !level || level>3) return 0;
    BOOT_U32 index=tag-20;
    return snapshot->TableIds[index]==owner->Tables[index].Id && physical==owner->Tables[index].Base &&
        level==(index<2?index+1:3) && ((view>>(12+(level-1)*9))&511)==X86_SURFACE_SLOT;
}

// ====== x86_surface_complete ======

int x86_surface_complete(const X86_SURFACE_SNAPSHOT *snapshot,BOOT_U32 seen,BOOT_U64 leaves)
{
    if (!snapshot) return 0;
    if (!registered) {
        for (BOOT_U32 byte=0; byte<sizeof(*snapshot); ++byte) if (((const unsigned char *)snapshot)[byte]) return 0;
        return !seen && !leaves;
    }
    if (registered->Count<3 || registered->Count>11) return 0;
    if (snapshot->Owner!=registered->Self || snapshot->Base!=registered->Data.Base ||
        snapshot->DataId!=registered->Data.Id || snapshot->Pages!=registered->Data.Pages ||
        leaves!=snapshot->Pages || seen!=(1U<<registered->Count)-1 || !registered_check || !registered_check(registered)) return 0;
    for (BOOT_U32 index=0; index<11; ++index)
        if (snapshot->TableIds[index]!=registered->Tables[index].Id) return 0;
    return 1;
}
