#include "validation.h"
#include "lib/memory/address_ranges.h"
#include "arch/x86/mm/devices/lapic.h"
#include "../gpu/validation.h"

// ====== fotografia UEFI coerente con il totale conservato dall'allocatore ===== //

static int map_consistent(const KERNEL_BOOT_CONTEXT *context, const KERNEL_PHYSICAL_ALLOCATOR *pool)
{
    KERNEL_MEMORY_INVENTORY inventory;
    KERNEL_MEMORY_REGION region;
    BOOT_U64 index, total = 0;
    if (!kernel_inventory_memory(context->MemoryMap, context->Info.MemoryMapBytes,
                                  context->Info.MemoryDescriptorBytes, &inventory)) return 0;
    for (index = 0; index < inventory.Regions; ++index) {
        BOOT_U64 pages;
        if (!kernel_read_region(context->MemoryMap, context->Info.MemoryMapBytes,
                                context->Info.MemoryDescriptorBytes, index, &region)) return 0;
        pages = kernel_physical_pages(region.Base, region.Pages, region.Attributes, region.Type, pool->PhysicalBits);
        if (pages > ~0ULL - total) return 0;
        total += pages;
    }
    return total == pool->TotalPages;
}

// ====== buffer kernel leggibili disgiunti e output fuori dalla vista ricorsiva ===== //

int x86_paging_buffers_valid(const KERNEL_BOOT_CONTEXT *context,
                         const KERNEL_PHYSICAL_ALLOCATOR *pool, const X86_PAGING_INVENTORY *output)
{
    BOOT_U64 map, bytes, stride, target = (BOOT_U64)output;
    if (!context || !pool || !output || ((BOOT_U64)context & 7) ||
        ((BOOT_U64)pool & 7) || (target & 7) || target > ~0ULL - sizeof(*output)) return 0;
    map = (BOOT_U64)context->MemoryMap; bytes = context->Info.MemoryMapBytes;
    stride = context->Info.MemoryDescriptorBytes;
    if (!map || !bytes || bytes > 1048576 || stride < 40 || stride > 4096 ||
        bytes % stride || bytes / stride > 4096 || map > ~0ULL - bytes) return 0;
    if (os_address_ranges_overlap(target, sizeof(*output), map, bytes) ||
        os_address_ranges_overlap(target, sizeof(*output), (BOOT_U64)context, sizeof(*context)) ||
        os_address_ranges_overlap(target, sizeof(*output), (BOOT_U64)pool, sizeof(*pool)) ||
        os_address_ranges_overlap(target, sizeof(*output), X86_LAPIC_VIRTUAL-4096,12288) ||
        os_address_ranges_overlap(target, sizeof(*output), 128ULL << 39, 1ULL << 39)) return 0;
    return kernel_physical_validate(pool) && map_consistent(context, pool) && x86_paging_gpu_inputs(context,pool,output);
}

// ====== tabella ereditata interamente dentro EfiLoaderData ordinaria WB ===== //

static int loader_table_reserved(const KERNEL_BOOT_CONTEXT *context,
                           const KERNEL_PHYSICAL_ALLOCATOR *pool, BOOT_U64 physical)
{
    KERNEL_MEMORY_REGION region;
    BOOT_U64 index, matches = 0;
    if (!x86_paging_entry_valid(physical | 3, 0, pool->PhysicalBits)) return 0;
    for (index = 0; index < context->Info.MemoryMapBytes / context->Info.MemoryDescriptorBytes; ++index) {
        if (!kernel_read_region(context->MemoryMap, context->Info.MemoryMapBytes,
                                context->Info.MemoryDescriptorBytes, index, &region) ||
            !kernel_region_valid(region.Base, region.Pages, region.Attributes, region.Type)) return 0;
        if (!kernel_regions_overlap(physical, 1, region.Base, region.Pages)) continue;
        if (region.Type != 2 || !(region.Attributes & 8) || (region.Attributes & ~0x1fULL) ||
            physical < region.Base || physical - region.Base > (region.Pages - 1) * 4096) return 0;
        ++matches;
    }
    if (matches != 1) return 0;
    for (index = 0; index < pool->Count; ++index)
        if (kernel_regions_overlap(physical, 1, pool->Free[index].Base, pool->Free[index].Pages)) return 0;
    for (index = 0; index < KERNEL_PHYSICAL_LEASES; ++index)
        if (pool->Live[index].Id && kernel_regions_overlap(physical, 1, pool->Live[index].Base, pool->Live[index].Pages)) return 0;
    return 1;
}

// ====== x86_paging_table_source ======

BOOT_U32 x86_paging_table_source(const KERNEL_BOOT_CONTEXT *context,
    const KERNEL_PHYSICAL_ALLOCATOR *pool, const X86_PAGING_VIDEO_AUTH *video,
    BOOT_U64 physical)
{
    BOOT_U32 surface=x86_surface_source(pool,physical);
    if (surface) return surface;
    BOOT_U32 iommu=x86_iommu_source(pool,physical);
    if (iommu) return iommu;
    BOOT_U32 dma=x86_dma_source(pool,physical);
    if (dma) return dma;
    BOOT_U32 heap=x86_heap_source(pool,physical);
    if (heap) return heap;
    BOOT_U32 index;
    if (video) for (index = 0; index < video->Region.TableCount; ++index) {
        const KERNEL_PHYSICAL_LEASE *lease = &video->Tables[index];
        if (lease->Base != physical) continue;
        return lease->Pages == 1 && kernel_physical_is_pinned(pool, lease) ? index + 2 : 0;
    }
    {
        BOOT_U32 gpu=x86_paging_gpu_source(pool,physical);
        return gpu ? gpu : loader_table_reserved(context,pool,physical) ? 1U : 0U;
    }
}

// ====== video_aperture_reserved ======

static int video_aperture_reserved(const KERNEL_BOOT_CONTEXT *context,
    const KERNEL_PHYSICAL_ALLOCATOR *pool, const X86_PAGING_VIDEO_RECORD *video)
{
    BOOT_U64 index;
    for (index = 0; index < context->Info.MemoryMapBytes / context->Info.MemoryDescriptorBytes; ++index) {
        KERNEL_MEMORY_REGION region;
        if (!kernel_read_region(context->MemoryMap, context->Info.MemoryMapBytes,
            context->Info.MemoryDescriptorBytes, index, &region)) return 0;
        if (kernel_regions_overlap(video->Physical, video->AperturePages, region.Base, region.Pages) &&
            ((region.Type != 0 && region.Type != 11) || (region.Attributes & 0x3000))) return 0;
    }
    for (index = 0; index < pool->Count; ++index)
        if (kernel_regions_overlap(video->Physical, video->AperturePages,
            pool->Free[index].Base, pool->Free[index].Pages)) return 0;
    for (index = 0; index < KERNEL_PHYSICAL_LEASES; ++index)
        if (pool->Live[index].Id && kernel_regions_overlap(video->Physical, video->AperturePages,
            pool->Live[index].Base, pool->Live[index].Pages)) return 0;
    return 1;
}

// ====== x86_paging_video_inputs ======

int x86_paging_video_inputs(const KERNEL_BOOT_CONTEXT *context,
    const KERNEL_PHYSICAL_ALLOCATOR *pool, const X86_PAGING_VIDEO_AUTH *video,
    const X86_PAGING_INVENTORY *output)
{
    const OS_FRAMEBUFFER *fb = &context->Info.Framebuffer;
    const X86_PAGING_VIDEO_RECORD *region;
    BOOT_U64 offset, used;
    BOOT_U32 index, other;
    if (!video || ((BOOT_U64)video & 7) || video->Self != (BOOT_U64)video ||
        video->Ready != 1 || video->Root != context->Info.PageTableRoot ||
        video->PhysicalBits != pool->PhysicalBits ||
        os_address_ranges_overlap((BOOT_U64)output, sizeof(*output), (BOOT_U64)video, sizeof(*video))) return 0;
    region = &video->Region;
    if (region->Owner != (BOOT_U64)video || region->Reserved ||
        !x86_paging_video_policy(region->Virtual, region->Physical, region->Pages,
            region->AperturePages, region->Cache, region->Pat, pool->PhysicalBits) ||
        region->TableCount != (region->Pages + 2 + 511) / 512 + 2 ||
        os_address_ranges_overlap((BOOT_U64)output, sizeof(*output), region->Virtual - 4096,
            (region->Pages + 2) * 4096)) return 0;
    used = fb_descriptor_bytes(fb->Base, fb->Capacity, fb->Width, fb->Height,
        fb->Stride, fb->Format, pool->PhysicalBits);
    offset = fb->Base & 4095;
    if (!used || used != fb->Used || region->Physical != (fb->Base & ~4095ULL) ||
        region->Pages != (offset + used + 4095) / 4096 ||
        region->AperturePages != (offset + fb->Capacity + 4095) / 4096 ||
        !video_aperture_reserved(context, pool, region)) return 0;
    for (index = 0; index < X86_PAGING_VIDEO_TABLES; ++index) {
        const KERNEL_PHYSICAL_LEASE *lease = &video->Tables[index];
        if (index >= region->TableCount) {
            if (lease->Base || lease->Pages || lease->Id || lease->Owner) return 0;
            continue;
        }
        if (lease->Pages != 1 || !kernel_physical_is_pinned(pool, lease)) return 0;
        for (other = 0; other < index; ++other)
            if (video->Tables[other].Base == lease->Base || video->Tables[other].Id == lease->Id) return 0;
    }
    return 1;
}

// ====== x86_paging_video_tables_complete ======

int x86_paging_video_tables_complete(const X86_PAGING_VIDEO_AUTH *video,
    const X86_PAGING_INVENTORY *inventory)
{
    BOOT_U32 index, seen = 0, branch = (BOOT_U32)(video->Region.Virtual >> 39);
    for (index = 0; index < sizeof(video->Region); ++index)
        if (((const unsigned char *)&video->Region)[index] !=
            ((const unsigned char *)&inventory->Video)[index]) return 0;
    for (index = 0; index < X86_PAGING_VIDEO_TABLES; ++index)
        if (inventory->VideoTableIds[index] != video->Tables[index].Id) return 0;
    for (index = 0; index < inventory->Count; ++index) {
        const X86_PAGING_RESOURCE *record = &inventory->Tables[index];
        BOOT_U32 slot;
        if (!record->Level) {
            if (record->Reserved) return 0;
            continue;
        }
        slot = (BOOT_U32)((record->View >> (12 + (record->Level - 1) * 9)) & 511);
        if (slot == branch) {
            if (!record->Reserved || record->Reserved > video->Region.TableCount ||
                record->Physical != video->Tables[record->Reserved - 1].Base ||
                (seen & (1U << (record->Reserved - 1)))) return 0;
            seen |= 1U << (record->Reserved - 1);
        } else if (record->Reserved && !x86_paging_gpu_record(inventory,record) &&
            !x86_iommu_record(&inventory->Iommu,record->Physical,record->View,record->Level,record->Reserved) &&
            !x86_surface_record(&inventory->Surface,record->Physical,record->View,record->Level,record->Reserved) &&
            !x86_dma_record(&inventory->Dma,record->Physical,record->View,record->Level,record->Reserved) &&
            !x86_heap_record(&inventory->Heap,record->Physical,record->View,record->Level,record->Reserved)) return 0;
    }
    return seen == (1U << video->Region.TableCount) - 1;
}
