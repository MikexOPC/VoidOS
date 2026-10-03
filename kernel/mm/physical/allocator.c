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
    // Coalescere prima di inserire evita un record temporaneo anche a capacita piena.
    if (index && owner->Free[index - 1].Base + owner->Free[index - 1].Pages * 4096 == base) {
        owner->Free[index - 1].Pages += pages;
        if (index < owner->Count && base + pages * 4096 == owner->Free[index].Base) {
            owner->Free[index - 1].Pages += owner->Free[index].Pages;
            remove_extent(owner, index);
        }
        return;
    }
    if (index < owner->Count && base + pages * 4096 == owner->Free[index].Base) {
        owner->Free[index].Base = base;
        owner->Free[index].Pages += pages;
        return;
    }
    for (next = owner->Count; next > index; --next) owner->Free[next] = owner->Free[next - 1];
    owner->Free[index] = (KERNEL_PHYSICAL_EXTENT){base, pages};
    ++owner->Count;
}

// ====== un rilascio adiacente non richiede spazio aggiuntivo nei metadati ===== //

static int insertion_fits(const KERNEL_PHYSICAL_ALLOCATOR *owner, BOOT_U64 base, BOOT_U64 pages)
{
    BOOT_U32 index;
    if (owner->Count < KERNEL_PHYSICAL_EXTENTS) return 1;
    for (index = 0; index < owner->Count; ++index)
        if (owner->Free[index].Base + owner->Free[index].Pages * 4096 == base ||
            base + pages * 4096 == owner->Free[index].Base) return 1;
    return 0;
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
        if (!lease->Id) { if (lease->Base || lease->Pages || lease->Owner || owner->Pins[index]) return 0; continue; }
        if (owner->Pins[index] > 1) return 0;
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

// ====== first fit allineato con split limitato e identificatore senza wrap ===== //

int kernel_physical_allocate_aligned(KERNEL_PHYSICAL_ALLOCATOR *owner, BOOT_U64 pages,
                                     BOOT_U64 alignment_pages, KERNEL_PHYSICAL_LEASE *output)
{
    BOOT_U32 slot, index, next;
    BOOT_U64 base = 0, prefix = 0, suffix = 0;
    KERNEL_PHYSICAL_LEASE lease;
    if (!owner || !output || !pages || overlaps(owner, sizeof(*owner), output, sizeof(*output)) ||
        !kernel_physical_validate(owner) || owner->NextId == ~0ULL ||
        output->Base || output->Pages || output->Id || output->Owner) return 0;
    for (slot = 0; slot < KERNEL_PHYSICAL_LEASES; ++slot) if (!owner->Live[slot].Id) break;
    if (slot == KERNEL_PHYSICAL_LEASES) return 0;
    for (index = 0; index < owner->Count; ++index) {
        const KERNEL_PHYSICAL_EXTENT *range = &owner->Free[index];
        base = kernel_physical_aligned_base(range->Base, range->Pages, pages, alignment_pages, owner->PhysicalBits);
        if (!base) continue;
        prefix = (base - range->Base) / 4096;
        suffix = range->Pages - prefix - pages;
        if (!prefix || !suffix || owner->Count < KERNEL_PHYSICAL_EXTENTS) break;
    }
    if (index == owner->Count) return 0;
    lease = (KERNEL_PHYSICAL_LEASE){base, pages, owner->NextId, (BOOT_U64)owner};
    if (prefix && suffix) {
        for (next = owner->Count; next > index + 1; --next) owner->Free[next] = owner->Free[next - 1];
        owner->Free[index].Pages = prefix;
        owner->Free[index + 1] = (KERNEL_PHYSICAL_EXTENT){base + pages * 4096, suffix};
        ++owner->Count;
    } else if (prefix) {
        owner->Free[index].Pages = prefix;
    } else if (suffix) {
        owner->Free[index] = (KERNEL_PHYSICAL_EXTENT){base + pages * 4096, suffix};
    } else remove_extent(owner, index);
    owner->FreePages -= pages;
    ++owner->NextId;
    owner->Live[slot] = lease;
    *output = lease;
    return 1;
}

// ====== compatibilita dei chiamanti esistenti con allineamento minimo di una pagina ===== //

int kernel_physical_allocate(KERNEL_PHYSICAL_ALLOCATOR *owner, BOOT_U64 pages, KERNEL_PHYSICAL_LEASE *output)
{
    return kernel_physical_allocate_aligned(owner, pages, 1, output);
}

// ====== rilascio esatto respinge handle obsoleti o alterati ====== //

int kernel_physical_release(KERNEL_PHYSICAL_ALLOCATOR *owner, KERNEL_PHYSICAL_LEASE *lease)
{
    BOOT_U32 slot;
    if (!owner || !lease || overlaps(owner, sizeof(*owner), lease, sizeof(*lease)) ||
        !kernel_physical_validate(owner) || !lease->Id || lease->Owner != (BOOT_U64)owner) return 0;
    for (slot = 0; slot < KERNEL_PHYSICAL_LEASES; ++slot) {
        const KERNEL_PHYSICAL_LEASE *live = &owner->Live[slot];
        if (live->Id == lease->Id && live->Base == lease->Base && live->Pages == lease->Pages) break;
    }
    if (slot == KERNEL_PHYSICAL_LEASES || owner->Pins[slot] || !insertion_fits(owner, lease->Base, lease->Pages)) return 0;
    insert_extent(owner, lease->Base, lease->Pages);
    owner->FreePages += lease->Pages;
    owner->Live[slot] = (KERNEL_PHYSICAL_LEASE){0};
    *lease = (KERNEL_PHYSICAL_LEASE){0};
    return 1;
}

// ====== pin esclusivo usato dal mapper prima di pubblicare una PTE ====== //

int kernel_physical_pin(KERNEL_PHYSICAL_ALLOCATOR *owner, const KERNEL_PHYSICAL_LEASE *lease, BOOT_U32 pin)
{
    BOOT_U32 slot;
    if (!owner || !lease || pin > 1 || !kernel_physical_validate(owner) || lease->Owner != (BOOT_U64)owner || !lease->Id) return 0;
    for (slot = 0; slot < KERNEL_PHYSICAL_LEASES; ++slot) {
        const KERNEL_PHYSICAL_LEASE *live = &owner->Live[slot];
        if (live->Id == lease->Id && live->Base == lease->Base && live->Pages == lease->Pages) {
            if (owner->Pins[slot] == pin) return 0;
            owner->Pins[slot] = pin;
            return 1;
        }
    }
    return 0;
}

// ====== identita del frame bloccato verificata senza modificare lo stato ====== //

int kernel_physical_is_pinned(const KERNEL_PHYSICAL_ALLOCATOR *owner, const KERNEL_PHYSICAL_LEASE *lease)
{
    BOOT_U32 slot;
    if (!owner || !lease || !kernel_physical_validate(owner) || !lease->Id || lease->Owner != (BOOT_U64)owner) return 0;
    for (slot = 0; slot < KERNEL_PHYSICAL_LEASES; ++slot) {
        const KERNEL_PHYSICAL_LEASE *live = &owner->Live[slot];
        if (live->Id == lease->Id && live->Base == lease->Base && live->Pages == lease->Pages) return owner->Pins[slot] == 1;
    }
    return 0;
}
