#ifndef OS_X86_SCHEDULER_STACKS_H
#define OS_X86_SCHEDULER_STACKS_H

#include "context.h"
#include "kernel/mm/physical/allocator.h"
#include "arch/x86/kernel/descriptors/stacks.h"
#include "arch/x86/mm/regions/mapping.h"
#include "arch/x86/mm/devices/lapic.h"

#define X86_TASK_STACK_SLOTS 8U
#define X86_TASK_STACK_PAGES 16U
#define X86_TASK_STACK_STRIDE 18U
#define X86_TASK_STACK_LEAF 320U
#define X86_TASK_STACK_GUARD (X86_LAYOUT_WINDOWS_VIRTUAL + 0x140000ULL)
#define X86_TASK_STACK_LEASED 1U
#define X86_TASK_STACK_MAPPING 2U
#define X86_TASK_STACK_READY 3U

typedef struct {
    uint64_t  self;
    uint64_t  root;
    uint64_t  generation;
    KERNEL_PHYSICAL_ALLOCATOR *pool;
    KERNEL_PHYSICAL_LEASE lease;
    uint32_t  index;
    uint32_t  stage;
} X86_TASK_STACK;

_Static_assert(sizeof(X86_TASK_STACK) == 72, "Stack owner bounded");
_Static_assert(X86_TASK_STACK_BYTES == X86_TASK_STACK_PAGES * 4096ULL, "Stack geometry");
_Static_assert(((X86_TASK_STACK_GUARD >> 12) & 511) == X86_TASK_STACK_LEAF, "Task PT leaf");
_Static_assert((X86_TASK_STACK_GUARD >> 21) == (X86_KERNEL_PAGE_WINDOW >> 21), "Inherited branch");
_Static_assert(X86_TASK_STACK_LEAF + X86_TASK_STACK_SLOTS * X86_TASK_STACK_STRIDE <= 512, "PT budget");
_Static_assert(X86_TASK_STACK_LEAF > X86_LAPIC_LEAF &&
    X86_TASK_STACK_LEAF >= X86_REGION_LEAF + X86_REGION_SLOTS * X86_REGION_STRIDE &&
    X86_TASK_STACK_LEAF > X86_KERNEL_MAIN_LEAF + 18, "Known live regions precede task stacks");

// ====== stack_resource_owner ======

// Stable trusted typed objects only; caller supplies Rust RESERVED handle.
// Failure may retain a partial owner. ABORT/COLLECT only after release succeeds.
uint64_t x86_task_stack_base(uint32_t index);
int x86_task_stack_acquire(X86_TASK_STACK *owner, KERNEL_PHYSICAL_ALLOCATOR *pool,
                          uint64_t root, uint32_t index, uint64_t generation);
int x86_task_stack_validate(const X86_TASK_STACK *owner);
int x86_task_stack_release(X86_TASK_STACK *owner, uint32_t index, uint64_t generation);

// ====== stack_mapping_port_contract ======

// Private platform ports implemented in mapping/native.c; host fixtures mock CPU/views.
// dispatcher: exactly 1 iff BSP/IF=DF=0, outside IRQ/NMI and all task stacks,
// root/recursive branch/NXE/WP valid, slot ownership and stable metadata verified.
int x86_task_stack_dispatcher(const X86_TASK_STACK *owner);
// mode0: all 18 slot PTEs exactly zero; mode1: 16 lease frames supervisor RW/XD,
// guards zero, parent permissions valid, only A/D differences permitted.
int x86_task_stack_mapping_valid(const X86_TASK_STACK *owner, uint32_t mode);
// map must only publish into an empty owned slot, invalidate locally, then readback.
// Failure may leave partial owned PTEs; revoke handles both partial/full mappings.
int x86_task_stack_map(const X86_TASK_STACK *owner);
// revoke preflights the whole slot; never erase foreign/corrupt PTEs. Verify absent
// after invalidation before returning success. UP only; no remote TLB guarantee.
int x86_task_stack_revoke(const X86_TASK_STACK *owner);
// zero all 65536 owned bytes and readback. No callbacks to tasks; no borrowed view.
int x86_task_stack_zero(const X86_TASK_STACK *owner);

#endif
