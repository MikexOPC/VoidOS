#ifndef OS_KERNEL_SCHEDULER_INTEGRATION_H
#define OS_KERNEL_SCHEDULER_INTEGRATION_H
#include "kernel/mm/physical/allocator.h"

// ====== finite_scheduler_workload ======

// One-shot cooperative BSP diagnostic, main stack, before IRQ driver startup.
// No scheduler resources remain on success. Failure is terminal for caller.
int kernel_scheduler_run(KERNEL_PHYSICAL_ALLOCATOR *pool, BOOT_U64 root);
int kernel_scheduler_prepare(KERNEL_PHYSICAL_ALLOCATOR *pool, BOOT_U64 root);

#endif
