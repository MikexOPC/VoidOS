#include "kernel/mm/physical/allocator.h"

// ====== mock_only_component_budget ======

#ifndef PHYSICAL_MODEL_EXTERNAL_READ
int kernel_physical_budget_read(const KERNEL_PHYSICAL_ALLOCATOR *owner, BOOT_U64 *output) {
    if (!owner || !output) return 0;
    *output = owner->FreePages;
    return 1;
}
#endif

// ====== mock_only_generation ======

int kernel_physical_generation_read(const KERNEL_PHYSICAL_ALLOCATOR *owner, BOOT_U64 *output) {
    if (!owner || !output) return 0;
    *output = owner->Generation;
    return 1;
}

// ====== mock_only_budget_comparisons ======

int kernel_physical_budget_matches(const KERNEL_PHYSICAL_ALLOCATOR *owner, BOOT_U64 expected) {
    BOOT_U64 value;
    return kernel_physical_budget_read(owner, &value) && value == expected;
}
int kernel_physical_budget_at_least(const KERNEL_PHYSICAL_ALLOCATOR *owner, BOOT_U64 minimum) {
    BOOT_U64 value;
    return kernel_physical_budget_read(owner, &value) && value >= minimum;
}

// ====== overlap_private ======

static int overlap_private(BOOT_U64 first, BOOT_U64 pages, BOOT_U64 second, BOOT_U64 other) {
    if (!pages || !other || pages > (~0ULL - first) / 4096 || other > (~0ULL - second) / 4096) return 1;
    return first < second + other * 4096 && second < first + pages * 4096;
}

// ====== mock_only_range_query ======

int kernel_physical_range_unowned(const KERNEL_PHYSICAL_ALLOCATOR *owner, BOOT_U64 base, BOOT_U64 pages) {
    if (!owner || owner->Count > KERNEL_PHYSICAL_EXTENTS || !pages || (base & 4095) ||
        pages > (~0ULL - base) / 4096) return 0;
    for (BOOT_U32 index = 0; index < owner->Count; ++index)
        if (overlap_private(base, pages, owner->Free[index].Base, owner->Free[index].Pages)) return 0;
    for (BOOT_U32 index = 0; index < KERNEL_PHYSICAL_LEASES; ++index)
        if (owner->Live[index].Id && overlap_private(base, pages, owner->Live[index].Base, owner->Live[index].Pages)) return 0;
    return 1;
}

// ====== mock_only_lease_query ======

int kernel_physical_lease_state(const KERNEL_PHYSICAL_ALLOCATOR *owner, const KERNEL_PHYSICAL_LEASE *lease, BOOT_U32 pin) {
    if (!owner || !lease || pin > 1) return 0;
    BOOT_U32 matches = 0;
    for (BOOT_U32 index = 0; index < KERNEL_PHYSICAL_LEASES; ++index) {
        const KERNEL_PHYSICAL_LEASE *live = &owner->Live[index];
        if (live->Id && live->Id == lease->Id && live->Owner == lease->Owner &&
            live->Base == lease->Base && live->Pages == lease->Pages && owner->Pins[index] == pin) ++matches;
    }
    return matches == 1;
}

// ====== mock_only_pinned_range ======

int kernel_physical_range_pinned(const KERNEL_PHYSICAL_ALLOCATOR *owner, BOOT_U64 base, BOOT_U64 pages) {
    if (!owner) return 0;
    BOOT_U32 matches = 0;
    for (BOOT_U32 index = 0; index < KERNEL_PHYSICAL_LEASES; ++index)
        if (owner->Live[index].Id && owner->Live[index].Owner == owner->Self && owner->Live[index].Base == base &&
            owner->Live[index].Pages == pages && owner->Pins[index] == 1) ++matches;
    return matches == 1;
}

#ifdef PHYSICAL_MODEL_RELEASE
// ====== mock_only_release_sequence ======

int kernel_physical_release_fits(const KERNEL_PHYSICAL_ALLOCATOR *owner, const KERNEL_PHYSICAL_LEASE *lease) {
    return owner && lease && owner->Count < KERNEL_PHYSICAL_EXTENTS;
}
int kernel_physical_release_after_unmap(KERNEL_PHYSICAL_ALLOCATOR *owner, KERNEL_PHYSICAL_LEASE *lease) {
    return kernel_physical_pin(owner, lease, 0) && kernel_physical_release(owner, lease);
}
#endif
