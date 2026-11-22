#ifndef X86_HEAP_AUTHORITY_H
#define X86_HEAP_AUTHORITY_H
#include "kernel/mm/physical/allocator.h"
#include "arch/x86/configs/layout.h"

// G3 H1-A: kernel heap at its own PML4 slot. One contiguous pinned RAM lease of 4096 pages (16 MiB)
// mapped supervisor RW/XD WB from 2 MiB into the slot; PD slots 0, 9 and 14 stay unmapped as guards.
// PD slots 10..13 hold four initially empty PTs reserved for up to 64 guarded task stacks (32-page
// stride: 1 lower guard, 16 stack pages, 15 unmapped guard pages). Fourteen owned tables in one
// pinned lease: PDPT, PD, eight heap PTs and four task-stack PTs.
#define X86_HEAP_SLOT         X86_LAYOUT_HEAP_SLOT
#define X86_HEAP_VIRTUAL      (X86_LAYOUT_SLOT_ADDRESS(X86_HEAP_SLOT) + 0x200000ULL)
#define X86_HEAP_PAGES        4096U
#define X86_HEAP_STACK_PD     10U
#define X86_HEAP_STACK_PTS    4U
#define X86_HEAP_STACK_SLOTS  64U
#define X86_HEAP_STACK_STRIDE 32U
#define X86_HEAP_STACK_BASE   (X86_LAYOUT_SLOT_ADDRESS(X86_HEAP_SLOT) + (BOOT_U64)X86_HEAP_STACK_PD * 0x200000ULL)
#define X86_HEAP_TABLES       (10U + X86_HEAP_STACK_PTS)
// Inventory tags (Reserved) 48-61, one per table; the table source is tag + 1.
#define X86_HEAP_TAG          48U
typedef struct {
    BOOT_U64  Owner, Base, Pages, DataId, TableBase, TableId;
} X86_HEAP_SNAPSHOT;
typedef struct {
    BOOT_U64  Self, Root, Pat;
    KERNEL_PHYSICAL_LEASE  Data, Tables;
    BOOT_U32  PhysicalBits, Ready;
} X86_HEAP_AUTH;
_Static_assert(sizeof(X86_HEAP_SNAPSHOT) == 48, "Heap snapshot ABI");
typedef int (*X86_HEAP_CHECK)(const X86_HEAP_AUTH *);

const X86_HEAP_AUTH *x86_heap_owner(void);
int x86_heap_register(const X86_HEAP_AUTH *expected, const X86_HEAP_AUTH *next, X86_HEAP_CHECK check);
int x86_heap_inputs(const KERNEL_PHYSICAL_ALLOCATOR *pool, BOOT_U64 root, BOOT_U64 output, BOOT_U64 bytes,
    X86_HEAP_SNAPSHOT *snapshot);
BOOT_U32 x86_heap_source(const KERNEL_PHYSICAL_ALLOCATOR *pool, BOOT_U64 physical);
BOOT_U32 x86_heap_leaf(BOOT_U64 entry, BOOT_U64 address);
int x86_heap_record(const X86_HEAP_SNAPSHOT *snapshot, BOOT_U64 physical, BOOT_U64 view, BOOT_U32 level, BOOT_U32 tag);
int x86_heap_complete(const X86_HEAP_SNAPSHOT *snapshot, BOOT_U32 seen, BOOT_U64 leaves);
// Level of table index: 1 PDPT, 2 PD, 3 the eight PTs.
#define X86_HEAP_LEVEL(index) ((index) == 0 ? 1U : (index) == 1 ? 2U : 3U)
#endif
