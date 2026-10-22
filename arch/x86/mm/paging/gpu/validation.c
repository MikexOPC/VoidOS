#include "validation.h"
#include "lib/memory/address_ranges.h"

// ====== reserved_private ======

static int reserved_private(const KERNEL_BOOT_CONTEXT *context,
    const KERNEL_PHYSICAL_ALLOCATOR *pool, BOOT_U64 physical)
{
    if (os_address_ranges_overlap(physical,4096,context->Info.Framebuffer.Base,
        context->Info.Framebuffer.Capacity)) return 0;
    for (BOOT_U64 index=0; index<context->Info.MemoryMapBytes/context->Info.MemoryDescriptorBytes; ++index) {
        KERNEL_MEMORY_REGION region;
        if (!kernel_read_region(context->MemoryMap,context->Info.MemoryMapBytes,
            context->Info.MemoryDescriptorBytes,index,&region)) return 0;
        if (kernel_regions_overlap(physical,1,region.Base,region.Pages) &&
            ((region.Type != 0 && region.Type != 11) || (region.Attributes & 0x3000))) return 0;
    }
    for (BOOT_U32 index=0; index<pool->Count; ++index)
        if (kernel_regions_overlap(physical,1,pool->Free[index].Base,pool->Free[index].Pages)) return 0;
    for (BOOT_U32 index=0; index<KERNEL_PHYSICAL_LEASES; ++index)
        if (pool->Live[index].Id && kernel_regions_overlap(physical,1,pool->Live[index].Base,pool->Live[index].Pages)) return 0;
    return 1;
}

// ====== x86_paging_gpu_inputs ======

int x86_paging_gpu_inputs(const KERNEL_BOOT_CONTEXT *context,
    const KERNEL_PHYSICAL_ALLOCATOR *pool, const X86_PAGING_INVENTORY *output)
{
    const X86_PAGING_GPU_AUTH *owner=x86_paging_gpu_owner();
    KERNEL_VGPU_PAGE_PLAN plan;
    BOOT_U64 index;
    if (!context || !pool || !output || context->Info.MemoryDescriptorBytes<40 ||
        context->Info.MemoryMapBytes % context->Info.MemoryDescriptorBytes) return 0;
    if (!owner) return 1;
    if (((BOOT_U64)owner & 7) || owner->Self != (BOOT_U64)owner || owner->Ready != 1 ||
        owner->Root != context->Info.PageTableRoot || owner->PhysicalBits != pool->PhysicalBits ||
        (owner->Pat & 255) != 6 || (owner->Cache & ~0x98ULL) ||
        os_address_ranges_overlap((BOOT_U64)output,sizeof(*output),(BOOT_U64)owner,sizeof(*owner)) ||
        os_address_ranges_overlap((BOOT_U64)output,sizeof(*output),X86_GPU_VIRTUAL-4096,18*4096) ||
        !kernel_vgpu_plan(&context->Info.Gpu,pool->PhysicalBits,&plan) || !plan.Count || !x86_paging_gpu_live()) return 0;
    index=((owner->Cache>>3)&3)|((owner->Cache>>5)&4);
    if (((owner->Pat>>(index*8)) & 255) != 0) return 0;
    for (BOOT_U32 byte=0; byte<sizeof(plan); ++byte)
        if (((const unsigned char *)&plan)[byte] != ((const unsigned char *)&owner->Plan)[byte]) return 0;
    for (BOOT_U32 page=0; page<plan.Count; ++page)
        if (!reserved_private(context,pool,plan.Physical[page])) return 0;
    for (BOOT_U32 table=0; table<3; ++table) {
        if (owner->Tables[table].Pages != 1 || !kernel_physical_is_pinned(pool,&owner->Tables[table])) return 0;
        for (BOOT_U32 other=0; other<table; ++other)
            if (owner->Tables[table].Base == owner->Tables[other].Base || owner->Tables[table].Id == owner->Tables[other].Id) return 0;
    }
    return 1;
}

// ====== x86_paging_gpu_source ======

BOOT_U32 x86_paging_gpu_source(const KERNEL_PHYSICAL_ALLOCATOR *pool, BOOT_U64 physical)
{
    const X86_PAGING_GPU_AUTH *owner=x86_paging_gpu_owner();
    if (owner) for (BOOT_U32 index=0; index<3; ++index)
        if (owner->Tables[index].Base == physical)
            return kernel_physical_is_pinned(pool,&owner->Tables[index]) ? 17+index : 0;
    return 0;
}

// ====== x86_paging_gpu_leaf ======

// Intel SDM3A 5.5.2: P0/RW1, supervisor U-S2=0, XD63; UC bits from PAT.
BOOT_U32 x86_paging_gpu_leaf(BOOT_U64 entry, BOOT_U64 address)
{
    const X86_PAGING_GPU_AUTH *owner=x86_paging_gpu_owner();
    BOOT_U64 physical=entry & 0x000ffffffffff000ULL;
    if (!owner) return 1;
    if (owner->Ready != 1 || !owner->Plan.Count || owner->Plan.Count>16) return 0;
    if ((address>>39) == X86_GPU_SLOT) {
        BOOT_U64 index;
        if (address<X86_GPU_VIRTUAL || (address & 4095)) return 0;
        index=(address-X86_GPU_VIRTUAL)/4096;
        return index<owner->Plan.Count && (entry & ~0x60ULL)==
            (owner->Plan.Physical[index]|owner->Cache|0x8000000000000003ULL) ? 4U : 0U;
    }
    for (BOOT_U32 index=0; index<owner->Plan.Count; ++index)
        if (physical == owner->Plan.Physical[index]) return 0;
    return 1;
}

// ====== x86_paging_gpu_record ======

int x86_paging_gpu_record(const X86_PAGING_INVENTORY *snapshot, const X86_PAGING_RESOURCE *record)
{
    BOOT_U32 slot, index;
    if (snapshot->Gpu.Ready != 1 || !record->Level || record->Level>3 ||
        record->Reserved<16 || record->Reserved>18) return 0;
    slot=(BOOT_U32)((record->View>>(12+(record->Level-1)*9)) & 511);
    index=record->Reserved-16;
    return slot==X86_GPU_SLOT && index==record->Level-1 &&
        record->Physical==snapshot->Gpu.Tables[index].Base;
}

// ====== x86_paging_gpu_complete ======

int x86_paging_gpu_complete(const X86_PAGING_INVENTORY *snapshot)
{
    const X86_PAGING_GPU_AUTH *owner=x86_paging_gpu_owner();
    BOOT_U32 seen=0;
    if (!owner) return snapshot->Gpu.Ready==0;
    for (BOOT_U32 byte=0; byte<sizeof(*owner); ++byte)
        if (((const unsigned char *)owner)[byte] != ((const unsigned char *)&snapshot->Gpu)[byte]) return 0;
    for (BOOT_U32 index=0; index<snapshot->Count; ++index) {
        const X86_PAGING_RESOURCE *record=&snapshot->Tables[index];
        if (record->Reserved>=32) {
            if (!x86_iommu_record(&snapshot->Iommu,record->Physical,record->View,record->Level,record->Reserved)) return 0;
            continue;
        }
        if (record->Reserved>=20) {
            if (!x86_surface_record(&snapshot->Surface,record->Physical,record->View,record->Level,record->Reserved)) return 0;
            continue;
        }
        if (record->Reserved<16) continue;
        if (!x86_paging_gpu_record(snapshot,record) || (seen & (1U<<(record->Reserved-16)))) return 0;
        seen |= 1U<<(record->Reserved-16);
    }
    return seen==7 && x86_paging_gpu_live();
}
