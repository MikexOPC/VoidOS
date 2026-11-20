#include "local.h"

static X86_SCHEDULER_LOCAL locals[X86_PERCPU_CAPACITY];

// ====== x86_scheduler_local_cpu ======

X86_SCHEDULER_LOCAL *x86_scheduler_local_cpu(uint32_t cpu) {
    return cpu < X86_PERCPU_CAPACITY ? &locals[cpu] : NULL;
}

// ====== owner_private ======

static const X86_CPU_LOCAL *owner_private(void)
{
    const X86_CPU_LOCAL *owner = x86_percpu_current_local();
    return owner && owner->Index < X86_PERCPU_CAPACITY &&
        owner->State == X86_PERCPU_ACTIVE && owner->Self == (uint64_t)(uintptr_t)owner ? owner : NULL;
}

// ====== x86_scheduler_local_current ======

X86_SCHEDULER_LOCAL *x86_scheduler_local_current(void)
{
    const X86_CPU_LOCAL *owner = owner_private();
    X86_SCHEDULER_LOCAL *local;
    if (!owner) return NULL;
    local = &locals[owner->Index];
    return local->self == (uint64_t)(uintptr_t)local &&
        local->owner == (uint64_t)(uintptr_t)owner ? local : NULL;
}

// ====== x86_scheduler_local_prepare ======

X86_SCHEDULER_LOCAL *x86_scheduler_local_prepare(void)
{
    const X86_CPU_LOCAL *owner = owner_private();
    X86_SCHEDULER_LOCAL *local;
    if (!owner) return NULL;
    local = &locals[owner->Index];
    if (!local->self && !local->owner) {
        local->current = KERNEL_SCHEDULER_NONE;
        local->owner = (uint64_t)(uintptr_t)owner;
        local->self = (uint64_t)(uintptr_t)local;
    }
    return x86_scheduler_local_current();
}
