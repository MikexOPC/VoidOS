#ifndef OS_X86_KERNEL_EMERGENCY_STACKS_H
#define OS_X86_KERNEL_EMERGENCY_STACKS_H
#include "kernel/mm/physical/allocator.h"
#include "arch/x86/configs/boot_profile.h"
#define X86_KERNEL_IST_GUARD (X86_LAYOUT_WINDOWS_VIRTUAL + 0x40000ULL)
#define X86_KERNEL_IST_BYTES 16384ULL
#define X86_KERNEL_IST_LEAF 64U
#define X86_KERNEL_MAIN_GUARD (X86_LAYOUT_WINDOWS_VIRTUAL + 0x60000ULL)
#define X86_KERNEL_MAIN_BASE (X86_KERNEL_MAIN_GUARD + 4096)
#define X86_KERNEL_MAIN_BYTES 65536ULL
#define X86_KERNEL_MAIN_TOP (X86_KERNEL_MAIN_BASE + X86_KERNEL_MAIN_BYTES)
#define X86_KERNEL_MAIN_LEAF 96U
typedef struct {
    BOOT_U64 Self, Root;
    KERNEL_PHYSICAL_ALLOCATOR *Pool;
    KERNEL_PHYSICAL_LEASE Lease;
    BOOT_U32 Ready, Committed;
    BOOT_U32 Profile, Reserved;
} X86_KERNEL_STACKS;
// Owner e pool non spostabili: storage valido per l'intera vita della TSS.
// Commit e permanente nel bootstrap UP; release e ammesso soltanto prima.
_Static_assert((X86_KERNEL_IST_GUARD >> 21) == (X86_KERNEL_PAGE_WINDOW >> 21), "IST e finestra condividono solo il ramo PT");
_Static_assert(X86_KERNEL_IST_GUARD > X86_KERNEL_PAGE_WINDOW + X86_KERNEL_WINDOW_BYTES + 4096, "IST fuori dalla finestra");
_Static_assert(((X86_KERNEL_IST_GUARD >> 12) & 511) == X86_KERNEL_IST_LEAF, "Geometria IST fissa");
_Static_assert((X86_KERNEL_MAIN_GUARD >> 21) == (X86_KERNEL_IST_GUARD >> 21), "Stack principale nello stesso ramo PT");
_Static_assert(((X86_KERNEL_MAIN_GUARD >> 12) & 511) == X86_KERNEL_MAIN_LEAF, "Geometria stack principale");
BOOT_U64 x86_ist_base(BOOT_U32 stack);
BOOT_U64 x86_ist_top(BOOT_U32 stack);
BOOT_U32 x86_ist_frame(BOOT_U64 physical, BOOT_U32 bits);
BOOT_U32 x86_ist_page(BOOT_U32 slot);
BOOT_U32 x86_stack_page(BOOT_U32 profile, BOOT_U32 slot);
BOOT_U32 x86_stack_frame(BOOT_U32 profile, BOOT_U64 physical, BOOT_U32 bits);
BOOT_U32 x86_main_pointer(BOOT_U64 pointer);
int x86_kernel_stacks_acquire_for(X86_KERNEL_STACKS *owner, KERNEL_PHYSICAL_ALLOCATOR *pool, BOOT_U64 root, BOOT_U32 profile);
int x86_kernel_stack_branch(BOOT_U64 root, BOOT_U32 bits);
int x86_kernel_stacks_acquire(X86_KERNEL_STACKS *owner, KERNEL_PHYSICAL_ALLOCATOR *pool, BOOT_U64 root);
int x86_kernel_stacks_validate(const X86_KERNEL_STACKS *owner);
int x86_kernel_stacks_release(X86_KERNEL_STACKS *owner);
int x86_kernel_stacks_commit(X86_KERNEL_STACKS *owner);
volatile BOOT_U64 *x86_stack_entries(BOOT_U32 level);
volatile unsigned char *x86_stack_bytes(BOOT_U32 stack);
void x86_stack_invalidate(BOOT_U64 address);
#endif
