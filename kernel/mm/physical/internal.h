#ifndef OS_KERNEL_PHYSICAL_INTERNAL_H
#define OS_KERNEL_PHYSICAL_INTERNAL_H
#include "allocator.h"

// ====== physical_core_contract ======

#define KPHYS_LOCK_RANK 300U
typedef struct {
    KERNEL_LOCK_CONTEXT *Context;
    KERNEL_LOCK_GUARD Guard;
    uint64_t SavedIf;
} KPHYS_ACCESS;

int kphys_enter(const KERNEL_PHYSICAL_ALLOCATOR *owner, KPHYS_ACCESS *access);
void kphys_leave(KPHYS_ACCESS *access);
int kphys_buffer(const KERNEL_PHYSICAL_ALLOCATOR *owner, const KPHYS_ACCESS *access,
    const void *buffer, size_t bytes, int writable);
int kphys_initialize(KERNEL_PHYSICAL_ALLOCATOR *owner, const unsigned char *map,
    BOOT_U64 bytes, BOOT_U64 stride, BOOT_U32 bits);
int kphys_validate(const KERNEL_PHYSICAL_ALLOCATOR *owner);
int kphys_allocate_aligned(KERNEL_PHYSICAL_ALLOCATOR *owner, BOOT_U64 pages,
    BOOT_U64 alignment_pages, KERNEL_PHYSICAL_LEASE *output);
int kphys_allocate_within(KERNEL_PHYSICAL_ALLOCATOR *owner, BOOT_U64 pages,
    BOOT_U64 alignment_pages, BOOT_U64 minimum, BOOT_U64 limit, KERNEL_PHYSICAL_LEASE *output);
int kphys_release(KERNEL_PHYSICAL_ALLOCATOR *owner, KERNEL_PHYSICAL_LEASE *lease);
int kphys_pin(KERNEL_PHYSICAL_ALLOCATOR *owner, const KERNEL_PHYSICAL_LEASE *lease, BOOT_U32 pin);
int kphys_is_pinned(const KERNEL_PHYSICAL_ALLOCATOR *owner, const KERNEL_PHYSICAL_LEASE *lease);
int kphys_release_capacity(const KERNEL_PHYSICAL_ALLOCATOR *owner, const KERNEL_PHYSICAL_LEASE *lease);
#endif
