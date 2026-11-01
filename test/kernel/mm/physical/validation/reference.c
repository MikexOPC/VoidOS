#include "kernel/mm/physical/internal.h"

// ====== reference_physical_validate ======

int reference_physical_validate(const KERNEL_PHYSICAL_ALLOCATOR *owner)
{
    BOOT_U32 index, other;
    BOOT_U64 free_pages = 0, live_pages = 0;
    if (!owner || owner->Ready != 1 || owner->Self != (BOOT_U64)owner ||
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
