#include "local.h"
#include "kernel/sync/internal.h"

static X86_SCHEDULER_LOCAL locals[X86_PERCPU_CAPACITY];
static X86_TASK_BINDING bindings[KERNEL_SCHEDULER_CAPACITY];

// ====== x86_scheduler_task_binding ======

X86_TASK_BINDING *x86_scheduler_task_binding(uint32_t index) {
    return index < KERNEL_SCHEDULER_CAPACITY ? &bindings[index] : NULL;
}

// ====== x86_scheduler_task_binding_init ======

int x86_scheduler_task_binding_init(uint32_t index, uint64_t allowed, uint32_t driver) {
    X86_TASK_BINDING *binding = x86_scheduler_task_binding(index);
    if (!binding ||
        !allowed ||
        driver > 1 ||
        (driver && allowed != 1) ||
        ks_atomic_load_acquire(&binding->Owner)
    )
        return 0;

    *binding = (X86_TASK_BINDING){0};
    binding->Allowed = allowed;
    binding->LastCpu = X86_PERCPU_CAPACITY;
    binding->Driver = driver;
    return 1;
}

// ====== x86_scheduler_task_claim ======

int x86_scheduler_task_claim(uint32_t index, uint32_t cpu, const KERNEL_LOCK_WAIT *wait) {
    X86_TASK_BINDING *binding = x86_scheduler_task_binding(index);
    KERNEL_DEADLINE deadline = {0};
    uint32_t polls = 0, started = 0;
    if (!binding ||
        cpu >= X86_PERCPU_CAPACITY ||
        !(binding->Allowed & (1ULL << cpu))
    )
        return 0;

    for (;;) {
        uint64_t owner = ks_atomic_load_acquire(&binding->Owner);
        if (!owner && ks_atomic_compare(&binding->Owner,0,(uint64_t)cpu + 1))
            break;
        if (owner == (uint64_t)cpu + 1 ||
            owner > X86_PERCPU_CAPACITY ||
            !wait ||
            !wait->MaxPolls
        )
            return 0;

        if (!started) {
            if (!kernel_deadline_start(&wait->Clock,wait->Ticks,&deadline)
            )
                return 0;

            started = 1;
        }
        if (polls >= wait->MaxPolls ||
            kernel_deadline_expired(&wait->Clock,&deadline)
        )
            return 0;

        ++polls;
        ks_pause();
    }
    (void)ks_atomic_load_acquire(&binding->Owner);
    if (binding->LastCpu < X86_PERCPU_CAPACITY &&
        binding->LastCpu != cpu && binding->Migrations != UINT64_MAX
    )
        ++binding->Migrations;

    binding->LastCpu = cpu;
    binding->Visits |= 1ULL << cpu;
    uint64_t dispatches = ks_atomic_load_relaxed(&binding->Dispatches);
    if (dispatches != UINT64_MAX)
        ks_atomic_store_release(&binding->Dispatches,dispatches + 1);

    return 1;
}

// ====== x86_scheduler_task_release ======

int x86_scheduler_task_release(uint32_t index, uint32_t cpu) {
    X86_TASK_BINDING *binding = x86_scheduler_task_binding(index);
    if (!binding ||
        cpu >= X86_PERCPU_CAPACITY ||
        ks_atomic_load_acquire(&binding->Owner) != (uint64_t)cpu + 1
    )
        return 0;

    ks_atomic_store_release(&binding->Owner,0);
    return 1;
}

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
