#include "authority.h"
#include "arch/x86/configs/layout.h"
#include "lib/memory/address_ranges.h"
static const X86_IOMMU_AUTH *registered;
static X86_IOMMU_CHECK registered_check;

// ====== x86_iommu_owner ======

const X86_IOMMU_AUTH *x86_iommu_owner(void) { return registered; }

// ====== x86_iommu_register ======

int x86_iommu_register(const X86_IOMMU_AUTH *expected,const X86_IOMMU_AUTH *next,X86_IOMMU_CHECK check)
{
    if (registered!=expected || (!next && check) || (next && (expected || !check ||
        next->Self!=(BOOT_U64)next || next->Ready!=1 || !next->Count || next->Count>8 || next->Reserved || next->Writable>1 || !check(next)))) return 0;
    registered_check=check; registered=next;
    return 1;
}

// ====== x86_iommu_page_safe ======

int x86_iommu_page_safe(const KERNEL_BOOT_CONTEXT *context,const KERNEL_PHYSICAL_ALLOCATOR *pool,BOOT_U64 physical)
{
    if (!context || !pool || pool->PhysicalBits<32 || pool->PhysicalBits>52 || physical<4096 ||
        (physical&4095) || physical>(1ULL<<pool->PhysicalBits)-4096 ||
        !context->Info.MemoryMapBytes || context->Info.MemoryMapBytes>1048576 || context->Info.MemoryDescriptorBytes<40 ||
        context->Info.MemoryMapBytes%context->Info.MemoryDescriptorBytes ||
        os_address_ranges_overlap(physical,4096,context->Info.Framebuffer.Base,context->Info.Framebuffer.Capacity)) return 0;
    for (BOOT_U64 index=0; index<context->Info.MemoryMapBytes/context->Info.MemoryDescriptorBytes; ++index) {
        KERNEL_MEMORY_REGION region;
        if (!kernel_read_region(context->MemoryMap,context->Info.MemoryMapBytes,context->Info.MemoryDescriptorBytes,index,&region)) return 0;
        if (kernel_regions_overlap(physical,1,region.Base,region.Pages) &&
            ((region.Type!=0 && region.Type!=11) || (region.Attributes&0x3000) || (region.Attributes && !(region.Attributes&1)))) return 0;
    }
    return kernel_physical_range_unowned(pool, physical, 1);
}

// ====== x86_iommu_inputs ======

int x86_iommu_inputs(const KERNEL_BOOT_CONTEXT *context,const KERNEL_PHYSICAL_ALLOCATOR *pool,
    BOOT_U64 output,BOOT_U64 bytes,X86_IOMMU_SNAPSHOT *snapshot)
{
    const X86_IOMMU_AUTH *owner=registered;
    if (!snapshot || ((BOOT_U64)snapshot&7) || (BOOT_U64)snapshot>~0ULL-sizeof(*snapshot) ||
        (owner && os_address_ranges_overlap((BOOT_U64)snapshot,sizeof(*snapshot),(BOOT_U64)owner,sizeof(*owner))) ||
        (pool && os_address_ranges_overlap((BOOT_U64)snapshot,sizeof(*snapshot),(BOOT_U64)pool,sizeof(*pool))) ||
        (context && os_address_ranges_overlap((BOOT_U64)snapshot,sizeof(*snapshot),(BOOT_U64)context,sizeof(*context))) ||
        os_address_ranges_overlap((BOOT_U64)snapshot,sizeof(*snapshot),X86_IOMMU_VIRTUAL-4096,17*4096)) return 0;
    *snapshot=(X86_IOMMU_SNAPSHOT){0};
    if (!owner) return 1;
    if (!context || !pool || owner->Self!=(BOOT_U64)owner || owner->Ready!=1 || owner->Reserved || owner->Writable>1 ||
        owner->Root!=context->Info.PageTableRoot || owner->PhysicalBits!=pool->PhysicalBits || !owner->Count || owner->Count>8 ||
        (owner->Cache&~0x98ULL) || (owner->Pat&255)!=6 || !registered_check || !registered_check(owner) ||
        os_address_ranges_overlap(output,bytes,(BOOT_U64)owner,sizeof(*owner)) ||
        os_address_ranges_overlap(output,bytes,X86_IOMMU_VIRTUAL-4096,17*4096)) return 0;
    BOOT_U64 slot=((owner->Cache>>3)&3)|((owner->Cache>>5)&4);
    if ((owner->Pat>>(slot*8))&255) return 0;
    snapshot->Owner=owner->Self; snapshot->Cache=owner->Cache; snapshot->Count=owner->Count; snapshot->Writable=owner->Writable;
    for (BOOT_U32 index=0; index<8; ++index) {
        if (index>=owner->Count) { if (owner->Base[index]) return 0; continue; }
        if (!x86_iommu_page_safe(context,pool,owner->Base[index])) return 0;
        for (BOOT_U32 other=0; other<index; ++other) if (owner->Base[index]==owner->Base[other]) return 0;
        snapshot->Base[index]=owner->Base[index];
    }
    for (BOOT_U32 index=0; index<3; ++index) {
        const KERNEL_PHYSICAL_LEASE *lease=&owner->Tables[index];
        if (lease->Pages!=1 || !kernel_physical_is_pinned(pool,lease)) return 0;
        for (BOOT_U32 other=0; other<index; ++other)
            if (lease->Base==owner->Tables[other].Base || lease->Id==owner->Tables[other].Id) return 0;
        snapshot->TableIds[index]=lease->Id;
    }
    return 1;
}

// ====== x86_iommu_source ======

BOOT_U32 x86_iommu_source(const KERNEL_PHYSICAL_ALLOCATOR *pool,BOOT_U64 physical)
{
    if (registered) for (BOOT_U32 index=0; index<3; ++index)
        if (registered->Tables[index].Base==physical) return kernel_physical_is_pinned(pool,&registered->Tables[index])?33+index:0;
    return 0;
}

// ====== x86_iommu_leaf ======

// Intel SDM3A5.5.4/5.6: supervisor RO/XD, PAT UC; even slots and edges are guards.
BOOT_U32 x86_iommu_leaf(BOOT_U64 entry,BOOT_U64 address)
{
    const X86_IOMMU_AUTH *owner=registered;
    if (!owner) return 1;
    if (!owner->Count || owner->Count>8 || owner->Writable>1) return 0;
    if (X86_LAYOUT_SLOT_OF(address)==X86_IOMMU_SLOT) {
        if (address<X86_IOMMU_VIRTUAL || (address&4095)) return 0;
        BOOT_U64 slot=(address-X86_IOMMU_VIRTUAL)/4096;
        return !(slot&1) && slot/2<owner->Count && (entry&~0x60ULL)==
            (owner->Base[slot/2]|owner->Cache|(owner->Writable<<1)|0x8000000000000001ULL)?7:0;
    }
    for (BOOT_U32 index=0; index<owner->Count; ++index)
        if ((entry&0x000ffffffffff000ULL)==owner->Base[index]) return 0;
    return 1;
}

// ====== x86_iommu_record ======

int x86_iommu_record(const X86_IOMMU_SNAPSHOT *snapshot,BOOT_U64 physical,BOOT_U64 view,BOOT_U32 level,BOOT_U32 tag)
{
    if (!registered || !snapshot || snapshot->Owner!=registered->Self || level<1 || level>3 || tag<32 || tag>34) return 0;
    BOOT_U32 index=tag-32;
    return level==index+1 && physical==registered->Tables[index].Base && snapshot->TableIds[index]==registered->Tables[index].Id &&
        ((view>>(12+(level-1)*9))&511)==X86_IOMMU_SLOT;
}

// ====== x86_iommu_complete ======

int x86_iommu_complete(const X86_IOMMU_SNAPSHOT *snapshot,BOOT_U32 seen,BOOT_U32 leaves)
{
    if (!snapshot) return 0;
    if (!registered) {
        for (BOOT_U32 byte=0; byte<sizeof(*snapshot); ++byte) if (((const unsigned char *)snapshot)[byte]) return 0;
        return !seen && !leaves;
    }
    if (!registered->Count || registered->Count>8 || registered->Ready!=1 || registered->Reserved || registered->Writable>1) return 0;
    if (snapshot->Owner!=registered->Self || snapshot->Cache!=registered->Cache || snapshot->Count!=registered->Count ||
        snapshot->Reserved || snapshot->Writable!=registered->Writable || seen!=7 || leaves!=registered->Count || !registered_check || !registered_check(registered)) return 0;
    for (BOOT_U32 index=0; index<8; ++index) if (snapshot->Base[index]!=registered->Base[index]) return 0;
    for (BOOT_U32 index=0; index<3; ++index) if (snapshot->TableIds[index]!=registered->Tables[index].Id) return 0;
    return 1;
}
