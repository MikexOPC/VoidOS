#ifndef OS_KERNEL_PHYSICAL_ALLOCATOR_H
#define OS_KERNEL_PHYSICAL_ALLOCATOR_H
#include "kernel/mm/inventory.h"
#define KERNEL_PHYSICAL_REGIONS 128U
#define KERNEL_PHYSICAL_EXTENTS 256U
#define KERNEL_PHYSICAL_LEASES 64U
typedef struct { BOOT_U64 Base, Pages; } KERNEL_PHYSICAL_EXTENT;
typedef struct { BOOT_U64 Base, Pages, Id, Owner; } KERNEL_PHYSICAL_LEASE;
typedef struct {
    BOOT_U64 Self, TotalPages, FreePages, NextId;
    BOOT_U32 Ready, PhysicalBits, Count, Reserved;
    KERNEL_PHYSICAL_EXTENT Free[KERNEL_PHYSICAL_EXTENTS];
    KERNEL_PHYSICAL_LEASE Live[KERNEL_PHYSICAL_LEASES];
    BOOT_U32 Pins[KERNEL_PHYSICAL_LEASES];
} KERNEL_PHYSICAL_ALLOCATOR;
_Static_assert(sizeof(KERNEL_PHYSICAL_ALLOCATOR) <= 8192, "Metadati allocatore entro 8 KiB");
BOOT_U64 kernel_physical_base(BOOT_U64 base);
BOOT_U64 kernel_physical_pages(BOOT_U64 base, BOOT_U64 pages, BOOT_U64 attributes, BOOT_U32 type, BOOT_U32 bits);
int kernel_physical_initialize(KERNEL_PHYSICAL_ALLOCATOR *owner, const unsigned char *map,
                               BOOT_U64 bytes, BOOT_U64 stride, BOOT_U32 bits);
int kernel_physical_validate(const KERNEL_PHYSICAL_ALLOCATOR *owner);
int kernel_physical_allocate(KERNEL_PHYSICAL_ALLOCATOR *owner, BOOT_U64 pages, KERNEL_PHYSICAL_LEASE *output);
// ====== allineamento in pagine potenza di due e base fisica verificata da Rust ===== //

BOOT_U64 kernel_physical_aligned_base(BOOT_U64 base, BOOT_U64 available, BOOT_U64 pages,
                                     BOOT_U64 alignment_pages, BOOT_U32 bits);
int kernel_physical_allocate_aligned(KERNEL_PHYSICAL_ALLOCATOR *owner, BOOT_U64 pages,
                                     BOOT_U64 alignment_pages, KERNEL_PHYSICAL_LEASE *output);
int kernel_physical_release(KERNEL_PHYSICAL_ALLOCATOR *owner, KERNEL_PHYSICAL_LEASE *lease);
int kernel_physical_pin(KERNEL_PHYSICAL_ALLOCATOR *owner, const KERNEL_PHYSICAL_LEASE *lease, BOOT_U32 pin);
int kernel_physical_is_pinned(const KERNEL_PHYSICAL_ALLOCATOR *owner, const KERNEL_PHYSICAL_LEASE *lease);
#endif
