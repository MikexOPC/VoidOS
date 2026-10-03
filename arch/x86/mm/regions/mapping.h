#ifndef OS_X86_KERNEL_REGION_MAPPING_H
#define OS_X86_KERNEL_REGION_MAPPING_H
#include "kernel/mm/physical/allocator.h"
#include "arch/x86/configs/boot_profile.h"
#define X86_REGION_GUARD 0x0000700000080000ULL
#define X86_REGION_SLOTS 4U
#define X86_REGION_PAGES 32U
#define X86_REGION_STRIDE 34U
#define X86_REGION_LEAF 128U
typedef struct { BOOT_U64 Owner, Id, Virtual, Pages; } X86_REGION_HANDLE;
typedef struct {
    BOOT_U64 Self, Root;
    KERNEL_PHYSICAL_ALLOCATOR *Pool;
    KERNEL_PHYSICAL_LEASE Live[X86_REGION_SLOTS];
    BOOT_U32 Ready, Reserved;
} X86_REGION_MAPPER;
_Static_assert(sizeof(X86_REGION_MAPPER) == 160, "Mapper metadata bounded to 160 bytes");
_Static_assert(X86_REGION_GUARD == 0x0000700000080000ULL && X86_REGION_SLOTS == 4 &&
               X86_REGION_STRIDE == 34 && X86_REGION_PAGES == 32, "C and Rust require the fixed arena profile");
_Static_assert(((X86_REGION_GUARD >> 12) & 511) == X86_REGION_LEAF, "Arena leaf matches virtual address");
_Static_assert((X86_REGION_GUARD >> 21) == (X86_KERNEL_PAGE_WINDOW >> 21), "Shared inherited PT branch");
_Static_assert(X86_REGION_LEAF + X86_REGION_SLOTS * X86_REGION_STRIDE <= 512, "Arena fits one PT");
BOOT_U64 x86_region_base(BOOT_U32 slot);
BOOT_U64 x86_region_frame(BOOT_U64 base, BOOT_U64 pages, BOOT_U32 bits);
BOOT_U32 x86_region_slot(BOOT_U64 address);
int x86_region_initialize(X86_REGION_MAPPER *owner, KERNEL_PHYSICAL_ALLOCATOR *pool, BOOT_U64 root);
int x86_region_validate(const X86_REGION_MAPPER *owner);
int x86_region_acquire(X86_REGION_MAPPER *owner, BOOT_U64 pages, BOOT_U64 alignment_pages, X86_REGION_HANDLE *output);
volatile unsigned char *x86_region_data(const X86_REGION_MAPPER *owner, const X86_REGION_HANDLE *handle);
int x86_region_release(X86_REGION_MAPPER *owner, X86_REGION_HANDLE *handle);
int x86_region_detach(X86_REGION_MAPPER *owner);
volatile BOOT_U64 *x86_region_entries(BOOT_U32 level);
volatile unsigned char *x86_region_bytes(BOOT_U32 slot);
void x86_region_invalidate(BOOT_U64 address);
#endif
