#include "allocator.h"

// ====== metadati distinti da output e buffer preso in prestito ====== //

static int overlaps(const void *first, BOOT_U64 bytes, const void *second, BOOT_U64 other)
{
    BOOT_U64 a = (BOOT_U64)first, b = (BOOT_U64)second;
    return a > ~0ULL - bytes || b > ~0ULL - other || (a < b + other && b < a + bytes);
}

// ====== rimozione e inserimento ordinato con coalescenza dei vicini ====== //

static void remove_extent(KERNEL_PHYSICAL_ALLOCATOR *owner, BOOT_U32 index)
{
    BOOT_U32 next;
    for (next = index + 1; next < owner->Count; ++next) owner->Free[next - 1] = owner->Free[next];
    --owner->Count;
    owner->Free[owner->Count] = (KERNEL_PHYSICAL_EXTENT){0};
}

static void insert_extent(KERNEL_PHYSICAL_ALLOCATOR *owner, BOOT_U64 base, BOOT_U64 pages)
{
    BOOT_U32 index = 0, next;
    while (index < owner->Count && owner->Free[index].Base < base) ++index;
    for (next = owner->Count; next > index; --next) owner->Free[next] = owner->Free[next - 1];
    owner->Free[index] = (KERNEL_PHYSICAL_EXTENT){base, pages};
    ++owner->Count;
    if (index && owner->Free[index - 1].Base + owner->Free[index - 1].Pages * 4096 == base) {
        owner->Free[index - 1].Pages += pages;
        remove_extent(owner, index);
        --index;
    }
    if (index + 1 < owner->Count && owner->Free[index].Base + owner->Free[index].Pages * 4096 == owner->Free[index + 1].Base) {
        owner->Free[index].Pages += owner->Free[index + 1].Pages;
        remove_extent(owner, index + 1);
    }
}

// ====== invarianti prima di ogni modifica e nessuna dereferenziazione fisica ====== //

int kernel_physical_validate(const KERNEL_PHYSICAL_ALLOCATOR *owner)
{
    BOOT_U32 index, other;
    BOOT_U64 free_pages = 0, live_pages = 0;
    if (!owner || owner->Ready != 1 || owner->Self != (BOOT_U64)owner || owner->Reserved ||
        owner->PhysicalBits < 32 || owner->PhysicalBits > 52 || !owner->NextId ||
        owner->Count > KERNEL_PHYSICAL_EXTENTS || !owner->TotalPages || owner->FreePages > owner->TotalPages) return 0;
    for (index = 0; index < owner->Count; ++index) {
        const KERNEL_PHYSICAL_EXTENT *range = &owner->Free[index];
        if (kernel_physical_pages(range->Base, range->Pages, 8, 7, owner->PhysicalBits) != range->Pages ||
            !range->Pages || range->Pages > owner->TotalPages - free_pages) return 0;
        if (index && owner->Free[index - 1].Base + owner->Free[index - 1].Pages * 4096 >= range->Base) return 0;
        free_pages += range->Pages;
    }
    for (index = 0; index < KERNEL_PHYSICAL_LEASES; ++index) {
        const KERNEL_PHYSICAL_LEASE *lease = &owner->Live[index];
        if (!lease->Id) { if (lease->Base || lease->Pages || lease->Owner) return 0; continue; }
        if (lease->Owner != (BOOT_U64)owner || lease->Id >= owner->NextId || !lease->Pages ||
            kernel_physical_pages(lease->Base, lease->Pages, 8, 7, owner->PhysicalBits) != lease->Pages ||
            lease->Pages > owner->TotalPages - live_pages) return 0;
        live_pages += lease->Pages;
        for (other = 0; other < owner->Count; ++other)
            if (kernel_regions_overlap(lease->Base, lease->Pages, owner->Free[other].Base, owner->Free[other].Pages)) return 0;
        for (other = 0; other < index; ++other)
            if (owner->Live[other].Id && (lease->Id == owner->Live[other].Id ||
                kernel_regions_overlap(lease->Base, lease->Pages, owner->Live[other].Base, owner->Live[other].Pages))) return 0;
    }
    return free_pages == owner->FreePages && live_pages == owner->TotalPages - free_pages;
}

// ====== due passaggi sulla mappa immutabile prima di pubblicare Ready ====== //

int kernel_physical_initialize(KERNEL_PHYSICAL_ALLOCATOR *owner, const unsigned char *map,
                               BOOT_U64 bytes, BOOT_U64 stride, BOOT_U32 bits)
{
    KERNEL_MEMORY_INVENTORY inventory;
    KERNEL_MEMORY_REGION region;
    BOOT_U64 index, total = 0;
    BOOT_U32 ranges = 0;
    if (!owner || bits < 32 || bits > 52 || overlaps(owner, sizeof(*owner), map, bytes)) return 0;
    for (index = 0; index < sizeof(*owner); ++index) if (((const unsigned char *)owner)[index]) return 0;
    if (!kernel_inventory_memory(map, bytes, stride, &inventory)) return 0;
    for (index = 0; index < inventory.Regions; ++index) {
        BOOT_U64 pages;
        if (!kernel_read_region(map, bytes, stride, index, &region)) return 0;
        pages = kernel_physical_pages(region.Base, region.Pages, region.Attributes, region.Type, bits);
        if (pages && (++ranges > KERNEL_PHYSICAL_REGIONS || pages > ~0ULL - total)) return 0;
        total += pages;
    }
    if (!total) return 0;
    for (index = 0; index < inventory.Regions; ++index) {
        BOOT_U64 pages;
        if (!kernel_read_region(map, bytes, stride, index, &region)) goto invalid;
        pages = kernel_physical_pages(region.Base, region.Pages, region.Attributes, region.Type, bits);
        if (pages) insert_extent(owner, region.Base, pages);
    }
    owner->Self = (BOOT_U64)owner;
    owner->TotalPages = owner->FreePages = total;
    owner->NextId = 1;
    owner->PhysicalBits = bits;
    owner->Ready = 1;
    if (kernel_physical_validate(owner)) return 1;
invalid:
    for (index = 0; index < sizeof(*owner); ++index) ((volatile unsigned char *)owner)[index] = 0;
    return 0;
}

// ====== assegnazione contigua con identificatore monotono senza wrap ====== //

int kernel_physical_allocate(KERNEL_PHYSICAL_ALLOCATOR *owner, BOOT_U64 pages, KERNEL_PHYSICAL_LEASE *output)
{
    BOOT_U32 slot, index;
    KERNEL_PHYSICAL_LEASE lease;
    if (!owner || !output || !pages || overlaps(owner, sizeof(*owner), output, sizeof(*output)) ||
        !kernel_physical_validate(owner) || owner->NextId == ~0ULL ||
        output->Base || output->Pages || output->Id || output->Owner) return 0;
    for (slot = 0; slot < KERNEL_PHYSICAL_LEASES; ++slot) if (!owner->Live[slot].Id) break;
    if (slot == KERNEL_PHYSICAL_LEASES) return 0;
    for (index = 0; index < owner->Count; ++index) if (owner->Free[index].Pages >= pages) break;
    if (index == owner->Count) return 0;
    lease = (KERNEL_PHYSICAL_LEASE){owner->Free[index].Base, pages, owner->NextId, (BOOT_U64)owner};
    owner->Free[index].Base += pages * 4096;
    owner->Free[index].Pages -= pages;
    if (!owner->Free[index].Pages) remove_extent(owner, index);
    owner->FreePages -= pages;
    ++owner->NextId;
    owner->Live[slot] = lease;
    *output = lease;
    return 1;
}

// ====== rilascio esatto respinge handle obsoleti o alterati ====== //

int kernel_physical_release(KERNEL_PHYSICAL_ALLOCATOR *owner, KERNEL_PHYSICAL_LEASE *lease)
{
    BOOT_U32 slot;
    if (!owner || !lease || overlaps(owner, sizeof(*owner), lease, sizeof(*lease)) ||
        !kernel_physical_validate(owner) || !lease->Id || lease->Owner != (BOOT_U64)owner ||
        owner->Count == KERNEL_PHYSICAL_EXTENTS) return 0;
    for (slot = 0; slot < KERNEL_PHYSICAL_LEASES; ++slot) {
        const KERNEL_PHYSICAL_LEASE *live = &owner->Live[slot];
        if (live->Id == lease->Id && live->Base == lease->Base && live->Pages == lease->Pages) break;
    }
    if (slot == KERNEL_PHYSICAL_LEASES) return 0;
    insert_extent(owner, lease->Base, lease->Pages);
    owner->FreePages += lease->Pages;
    owner->Live[slot] = (KERNEL_PHYSICAL_LEASE){0};
    *lease = (KERNEL_PHYSICAL_LEASE){0};
    return 1;
}
