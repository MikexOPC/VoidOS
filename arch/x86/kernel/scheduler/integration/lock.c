#include "kernel/scheduler/scheduler.h"
#include "arch/x86/kernel/cpu/percpu/internal.h"

#define X86_SCHEDULER_LOCK_RANK 900U

static KERNEL_LOCK scheduler_lock;
static KERNEL_LOCK_GUARD scheduler_guards[X86_PERCPU_CAPACITY];

// ====== held_cpu_private ======

static const X86_CPU_LOCAL *held_cpu_private(void) {
    uint64_t base = xp_gs_read();
    if ((base & 63U) ||
        !xp_image_contains((uintptr_t)base,sizeof(X86_CPU_LOCAL))
    )
        return NULL;
    const X86_CPU_LOCAL *local = (const X86_CPU_LOCAL *)(uintptr_t)base;
    if (local->Self != base ||
        local->Index >= X86_PERCPU_CAPACITY ||
        local->State != X86_PERCPU_ACTIVE ||
        xp_gs_self() != base ||
        ks_irq_state()
    )
        return NULL;
    const KERNEL_LOCK_GUARD *guard = &scheduler_guards[local->Index];
    const KERNEL_LOCK_CONTEXT *context = &local->Locks;
    if (guard->Self != (uintptr_t)guard ||
        guard->Context != context ||
        guard->Lock != &scheduler_lock ||
        guard->SavedIf ||
        !context->Depth ||
        context->Depth > KERNEL_LOCK_DEPTH
    )
        return NULL;
    const KERNEL_LOCK_FRAME *frame = &context->Frames[context->Depth - 1];
    return frame->Guard == (uintptr_t)guard && frame->Lock == (uintptr_t)&scheduler_lock &&
        frame->Token == guard->Token && frame->Ticket == guard->Ticket ? local : NULL;
}

// ====== lock_cpu_private ======

static const X86_CPU_LOCAL *lock_cpu_private(void)
{
    const X86_CPU_LOCAL *local = x86_percpu_current_local();
    return local && local->Index < X86_PERCPU_CAPACITY && local->State == X86_PERCPU_ACTIVE ? local : 0;
}

// ====== kernel_scheduler_context_valid_on ======

uint32_t kernel_scheduler_context_valid_on(uint32_t cpu)
{
    const X86_CPU_LOCAL *local = held_cpu_private();
    if (local)
        return local->Index == cpu;
    local = lock_cpu_private();
    return local && local->Index == cpu && kernel_scheduler_context_valid() == 1;
}

// ====== kernel_scheduler_lock_acquire ======

uint32_t kernel_scheduler_lock_acquire(void)
{
    const X86_CPU_LOCAL *local = lock_cpu_private();
    if (!local ||
        kernel_scheduler_context_valid() != 1
    )
        return 0;
    KERNEL_LOCK_CONTEXT *context = (KERNEL_LOCK_CONTEXT *)&local->Locks;
    if (!scheduler_lock.Ready && (local->Index != 0 ||
        !kernel_lock_initialize(&scheduler_lock, X86_SCHEDULER_LOCK_RANK))) return 0;
    return kernel_lock_acquire(&scheduler_lock, context, &scheduler_guards[local->Index]) == 1;
}

// ====== kernel_scheduler_lock_release ======

void kernel_scheduler_lock_release(void)
{
    const X86_CPU_LOCAL *local = held_cpu_private();
    if (!local ||
        !kernel_lock_release((KERNEL_LOCK_CONTEXT *)&local->Locks,&scheduler_guards[local->Index])
    )
        kernel_scheduler_fail_stop();
}
