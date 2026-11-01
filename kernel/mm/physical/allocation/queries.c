#include "kernel/mm/physical/internal.h"

// ====== kernel_physical_status ======

int kernel_physical_status(const KERNEL_PHYSICAL_ALLOCATOR *owner, KERNEL_PHYSICAL_STATUS *output) {
    KPHYS_ACCESS access = {0};
    KERNEL_PHYSICAL_STATUS draft = {0};
    int result = 0;
    if (!kphys_enter(owner, &access)) return 0;
    if (kphys_buffer(owner, &access, output, sizeof(*output), 1) && kphys_validate(owner)) {
        draft.Owner = owner->Self;
        draft.TotalPages = owner->TotalPages;
        draft.FreePages = owner->FreePages;
        draft.NextId = owner->NextId;
        draft.Count = owner->Count;
        draft.Generation = owner->Generation;
        for (BOOT_U32 index = 0; index < KERNEL_PHYSICAL_LEASES; ++index) {
            draft.LivePages += owner->Live[index].Pages;
            if (owner->Pins[index]) draft.PinnedPages += owner->Live[index].Pages;
        }
        *output = draft;
        result = 1;
    }
    kphys_leave(&access);
    return result;
}

// ====== kernel_physical_range_unowned ======

int kernel_physical_range_unowned(const KERNEL_PHYSICAL_ALLOCATOR *owner, BOOT_U64 base, BOOT_U64 pages) {
    KPHYS_ACCESS access = {0};
    int result = 0;
    if (!kphys_enter(owner, &access)) return 0;
    if (pages && kernel_physical_pages(base, pages, 8, 7, owner->PhysicalBits) == pages && kphys_validate(owner)) {
        result = 1;
        for (BOOT_U32 index = 0; index < owner->Count; ++index)
            if (kernel_regions_overlap(base, pages, owner->Free[index].Base, owner->Free[index].Pages)) result = 0;
        for (BOOT_U32 index = 0; index < KERNEL_PHYSICAL_LEASES; ++index)
            if (owner->Live[index].Id && kernel_regions_overlap(base, pages, owner->Live[index].Base, owner->Live[index].Pages)) result = 0;
    }
    kphys_leave(&access);
    return result;
}

// ====== kernel_physical_release_fits ======

int kernel_physical_release_fits(const KERNEL_PHYSICAL_ALLOCATOR *owner, const KERNEL_PHYSICAL_LEASE *lease) {
    KPHYS_ACCESS access = {0};
    int result = 0;
    if (!kphys_enter(owner, &access)) return 0;
    if (kphys_buffer(owner, &access, lease, sizeof(*lease), 0) && kphys_validate(owner)) {
        result = kphys_release_capacity(owner, lease);
    }
    kphys_leave(&access);
    return result;
}

// ====== kphys_release_capacity ======

int kphys_release_capacity(const KERNEL_PHYSICAL_ALLOCATOR *owner, const KERNEL_PHYSICAL_LEASE *lease) {
    for (BOOT_U32 slot = 0; slot < KERNEL_PHYSICAL_LEASES; ++slot) {
        const KERNEL_PHYSICAL_LEASE *live = &owner->Live[slot];
        if (!live->Id || live->Id != lease->Id || live->Owner != lease->Owner ||
            live->Base != lease->Base || live->Pages != lease->Pages) continue;
        int fits = owner->Count < KERNEL_PHYSICAL_EXTENTS;
        for (BOOT_U32 index = 0; !fits && index < owner->Count; ++index)
            fits = owner->Free[index].Base + owner->Free[index].Pages * 4096 == live->Base ||
                live->Base + live->Pages * 4096 == owner->Free[index].Base;
        return fits;
    }
    return 0;
}

// ====== read_counter_private ======

static int read_counter_private(const KERNEL_PHYSICAL_ALLOCATOR *owner, BOOT_U64 *output, int generation) {
    KPHYS_ACCESS access = {0};
    int result = 0;
    if (!kphys_enter(owner, &access)) return 0;
    if (kphys_buffer(owner, &access, output, sizeof(*output), 1) &&
        owner->TotalPages && owner->FreePages <= owner->TotalPages
    ) {
        *output = generation ? owner->Generation : owner->FreePages;
        result = 1;
    }
    kphys_leave(&access);
    return result;
}

// ====== kernel_physical_budget_read ======

int kernel_physical_budget_read(const KERNEL_PHYSICAL_ALLOCATOR *owner, BOOT_U64 *output) {
    return read_counter_private(owner, output, 0);
}

// ====== kernel_physical_generation_read ======

int kernel_physical_generation_read(const KERNEL_PHYSICAL_ALLOCATOR *owner, BOOT_U64 *output) {
    return read_counter_private(owner, output, 1);
}

// ====== kernel_physical_budget_matches ======

int kernel_physical_budget_matches(const KERNEL_PHYSICAL_ALLOCATOR *owner, BOOT_U64 expected) {
    BOOT_U64 available = 0;
    return kernel_physical_budget_read(owner, &available) && available == expected;
}

// ====== kernel_physical_budget_at_least ======

int kernel_physical_budget_at_least(const KERNEL_PHYSICAL_ALLOCATOR *owner, BOOT_U64 minimum) {
    BOOT_U64 available = 0;
    return kernel_physical_budget_read(owner, &available) && available >= minimum;
}

// ====== kernel_physical_lease_state ======

int kernel_physical_lease_state(const KERNEL_PHYSICAL_ALLOCATOR *owner, const KERNEL_PHYSICAL_LEASE *lease, BOOT_U32 expected_pin) {
    KPHYS_ACCESS access = {0};
    int result = 0;
    if (!kphys_enter(owner, &access)) return 0;
    if (expected_pin <= 1 && kphys_buffer(owner, &access, lease, sizeof(*lease), 0) && kphys_validate(owner)) {
        for (BOOT_U32 slot = 0; slot < KERNEL_PHYSICAL_LEASES; ++slot) {
            const KERNEL_PHYSICAL_LEASE *live = &owner->Live[slot];
            if (live->Id && live->Id == lease->Id && live->Owner == lease->Owner &&
                live->Base == lease->Base && live->Pages == lease->Pages) {
                result = owner->Pins[slot] == expected_pin;
                break;
            }
        }
    }
    kphys_leave(&access);
    return result;
}

// ====== kernel_physical_range_pinned ======

int kernel_physical_range_pinned(const KERNEL_PHYSICAL_ALLOCATOR *owner, BOOT_U64 base, BOOT_U64 pages) {
    KPHYS_ACCESS access = {0};
    int result = 0;
    if (!kphys_enter(owner, &access)) return 0;
    if (kphys_validate(owner)) {
        for (BOOT_U32 slot = 0; slot < KERNEL_PHYSICAL_LEASES; ++slot)
            if (owner->Live[slot].Id && owner->Live[slot].Base == base && owner->Live[slot].Pages == pages) {
                result = owner->Pins[slot] == 1;
                break;
            }
    }
    kphys_leave(&access);
    return result;
}
