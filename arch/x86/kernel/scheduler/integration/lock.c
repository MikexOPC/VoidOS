#include "kernel/scheduler/scheduler.h"
#include "arch/x86/kernel/cpu/percpu/percpu.h"

#define X86_SCHEDULER_LOCK_RANK 900U

static KERNEL_LOCK scheduler_lock;
static KERNEL_LOCK_GUARD scheduler_guards[X86_PERCPU_CAPACITY];

// ====== lock_cpu_private ======

static const X86_CPU_LOCAL *lock_cpu_private(void)
{
    const X86_CPU_LOCAL *local = x86_percpu_current_local();
    return local && local->Index < X86_PERCPU_CAPACITY && local->State == X86_PERCPU_ACTIVE ? local : 0;
}

// ====== kernel_scheduler_lock_acquire ======

uint32_t kernel_scheduler_lock_acquire(void)
{
    const X86_CPU_LOCAL *local = lock_cpu_private();
    KERNEL_LOCK_CONTEXT *context = x86_percpu_current_context();
    if (!local || !context) return 0;
    if (!scheduler_lock.Ready && (local->Index != 0 ||
        !kernel_lock_initialize(&scheduler_lock, X86_SCHEDULER_LOCK_RANK))) return 0;
    return kernel_lock_acquire(&scheduler_lock, context, &scheduler_guards[local->Index]) == 1;
}

// ====== kernel_scheduler_lock_release ======

void kernel_scheduler_lock_release(void)
{
    const X86_CPU_LOCAL *local = lock_cpu_private();
    KERNEL_LOCK_CONTEXT *context = x86_percpu_current_context();
    if (!local || !context || !kernel_lock_release(context, &scheduler_guards[local->Index]))
        kernel_scheduler_fail_stop();
}
