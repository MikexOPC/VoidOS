#ifndef OS_X86_AP_LOWMEMORY_H
#define OS_X86_AP_LOWMEMORY_H
#include "kernel/mm/physical/allocator.h"

typedef struct {
    BOOT_U64  Self;
    KERNEL_PHYSICAL_ALLOCATOR  *Pool;
    KERNEL_PHYSICAL_LEASE  Lease;
    BOOT_U32  State, Reserved;
} X86_AP_LOW_MEMORY;
_Static_assert(sizeof(X86_AP_LOW_MEMORY) == 56, "Low-memory owner layout");

BOOT_U32 x86_ap_low_geometry(BOOT_U64 base, BOOT_U64 pages);
int x86_ap_low_reserve(KERNEL_PHYSICAL_ALLOCATOR *pool, X86_AP_LOW_MEMORY *owner);
int x86_ap_low_valid(const X86_AP_LOW_MEMORY *owner);
int x86_ap_low_release(X86_AP_LOW_MEMORY *owner);
int x86_ap_low_start(KERNEL_PHYSICAL_ALLOCATOR *pool);
const X86_AP_LOW_MEMORY *x86_ap_low_current(void);
int x86_ap_low_hold(const X86_AP_LOW_MEMORY *owner, const void *borrower);
int x86_ap_low_unhold(const X86_AP_LOW_MEMORY *owner, const void *borrower);
int x86_ap_low_is_held(const X86_AP_LOW_MEMORY *owner, const void *borrower);
int x86_ap_low_freeze(const X86_AP_LOW_MEMORY *owner, const void *borrower);
int x86_ap_low_is_frozen(const X86_AP_LOW_MEMORY *owner, const void *borrower);
#endif
