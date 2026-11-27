#ifndef OS_KERNEL_PHYSICAL_ALLOCATOR_H
#define OS_KERNEL_PHYSICAL_ALLOCATOR_H
#include "kernel/mm/inventory.h"
#include "kernel/sync/lock.h"
#define KERNEL_PHYSICAL_REGIONS 128U
#define KERNEL_PHYSICAL_EXTENTS 4096U
// Generous ceiling for multi-process GUI, SHM surfaces, GPU buffers, and driver rings.
#define KERNEL_PHYSICAL_LEASES  4096U
typedef struct { BOOT_U64 Base, Pages; } KERNEL_PHYSICAL_EXTENT;
typedef struct { BOOT_U64 Base, Pages, Id, Owner; } KERNEL_PHYSICAL_LEASE;
typedef struct {
    BOOT_U64 Self, TotalPages, FreePages, NextId;
    // G5 V2: Generation advances on every allocate, release and pin change, so a cache keyed on
    // the allocator compares one counter instead of the whole metadata.
    BOOT_U32 Ready, PhysicalBits, Count, Generation;
    KERNEL_PHYSICAL_EXTENT Free[KERNEL_PHYSICAL_EXTENTS];
    KERNEL_PHYSICAL_LEASE Live[KERNEL_PHYSICAL_LEASES];
    BOOT_U32 Pins[KERNEL_PHYSICAL_LEASES];
    KERNEL_LOCK Gate;
} KERNEL_PHYSICAL_ALLOCATOR;
_Static_assert(sizeof(KERNEL_PHYSICAL_ALLOCATOR) <= 262144, "Metadati allocatore entro 256 KiB");
BOOT_U64 kernel_physical_base(BOOT_U64 base);
BOOT_U64 kernel_physical_pages(BOOT_U64 base, BOOT_U64 pages, BOOT_U64 attributes, BOOT_U32 type, BOOT_U32 bits);
int kernel_physical_initialize(KERNEL_PHYSICAL_ALLOCATOR *owner, const unsigned char *map,
                               BOOT_U64 bytes, BOOT_U64 stride, BOOT_U32 bits);
int kernel_physical_validate(const KERNEL_PHYSICAL_ALLOCATOR *owner);
int kernel_physical_allocate(KERNEL_PHYSICAL_ALLOCATOR *owner, BOOT_U64 pages, KERNEL_PHYSICAL_LEASE *output);
int kernel_physical_reserve_startup_page(KERNEL_PHYSICAL_ALLOCATOR *owner, KERNEL_PHYSICAL_LEASE *output);
int kernel_physical_allocate_within(KERNEL_PHYSICAL_ALLOCATOR *owner, BOOT_U64 pages,
    BOOT_U64 alignment_pages, BOOT_U64 minimum, BOOT_U64 limit, KERNEL_PHYSICAL_LEASE *output);
// ====== allineamento in pagine potenza di due e base fisica verificata da Rust ===== //

BOOT_U64 kernel_physical_aligned_base(BOOT_U64 base, BOOT_U64 available, BOOT_U64 pages,
                                     BOOT_U64 alignment_pages, BOOT_U32 bits);
int kernel_physical_allocate_aligned(KERNEL_PHYSICAL_ALLOCATOR *owner, BOOT_U64 pages,
                                     BOOT_U64 alignment_pages, KERNEL_PHYSICAL_LEASE *output);
int kernel_physical_release(KERNEL_PHYSICAL_ALLOCATOR *owner, KERNEL_PHYSICAL_LEASE *lease);
int kernel_physical_pin(KERNEL_PHYSICAL_ALLOCATOR *owner, const KERNEL_PHYSICAL_LEASE *lease, BOOT_U32 pin);
int kernel_physical_is_pinned(const KERNEL_PHYSICAL_ALLOCATOR *owner, const KERNEL_PHYSICAL_LEASE *lease);

// ====== coherent_physical_queries ======

typedef struct {
    BOOT_U64 Owner, TotalPages, FreePages, NextId;
    BOOT_U64 LivePages, PinnedPages;
    BOOT_U32 Count, Generation;
} KERNEL_PHYSICAL_STATUS;

int kernel_physical_status(const KERNEL_PHYSICAL_ALLOCATOR *owner, KERNEL_PHYSICAL_STATUS *output);
int kernel_physical_range_unowned(const KERNEL_PHYSICAL_ALLOCATOR *owner, BOOT_U64 base, BOOT_U64 pages);
int kernel_physical_release_fits(const KERNEL_PHYSICAL_ALLOCATOR *owner, const KERNEL_PHYSICAL_LEASE *lease);
int kernel_physical_release_after_unmap(KERNEL_PHYSICAL_ALLOCATOR *owner, KERNEL_PHYSICAL_LEASE *lease);
int kernel_physical_budget_read(const KERNEL_PHYSICAL_ALLOCATOR *owner, BOOT_U64 *output);
int kernel_physical_generation_read(const KERNEL_PHYSICAL_ALLOCATOR *owner, BOOT_U64 *output);
int kernel_physical_budget_matches(const KERNEL_PHYSICAL_ALLOCATOR *owner, BOOT_U64 expected);
int kernel_physical_budget_at_least(const KERNEL_PHYSICAL_ALLOCATOR *owner, BOOT_U64 minimum);
int kernel_physical_lease_state(const KERNEL_PHYSICAL_ALLOCATOR *owner, const KERNEL_PHYSICAL_LEASE *lease, BOOT_U32 expected_pin);
int kernel_physical_range_pinned(const KERNEL_PHYSICAL_ALLOCATOR *owner, BOOT_U64 base, BOOT_U64 pages);
#endif
