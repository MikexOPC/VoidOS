#include "kernel/scheduler/scheduler.h"
#include "arch/x86/kernel/cpu/percpu/percpu.h"
#include <stdio.h>
#include <stdlib.h>

static X86_CPU_LOCAL local;
static uint32_t present = 1, dispatcher = 1, checks, lock_calls;

// ====== backend_mocks ======

const X86_CPU_LOCAL *x86_percpu_current_local(void) { return present ? &local : NULL; }
KERNEL_LOCK_CONTEXT *x86_percpu_current_context(void) { return &local.Locks; }
uint32_t kernel_scheduler_context_valid(void) { return dispatcher; }
__declspec(noreturn) void kernel_scheduler_fail_stop(void) { exit(99); }

// ====== lock_mocks ======

int kernel_lock_initialize(KERNEL_LOCK *lock, uint32_t rank)
{
    (void)lock; (void)rank;
    ++lock_calls;
    return 0;
}

int kernel_lock_acquire(KERNEL_LOCK *lock, KERNEL_LOCK_CONTEXT *context, KERNEL_LOCK_GUARD *guard)
{
    (void)lock; (void)context; (void)guard;
    ++lock_calls;
    return 0;
}

int kernel_lock_release(KERNEL_LOCK_CONTEXT *context, KERNEL_LOCK_GUARD *guard)
{
    (void)context; (void)guard;
    ++lock_calls;
    return 0;
}

// ====== check_private ======

static void check_private(int condition)
{
    ++checks;
    if (!condition) { fprintf(stderr,"CPU authority check %u failed\n",checks); exit(1); }
}

// ====== main ======

int main(void)
{
    local.State = X86_PERCPU_ACTIVE;
    for (uint32_t cpu = 0; cpu < X86_PERCPU_CAPACITY; ++cpu) {
        local.Index = cpu;
        check_private(kernel_scheduler_context_valid_on(cpu) == 1);
        check_private(kernel_scheduler_context_valid_on((cpu + 1) % X86_PERCPU_CAPACITY) == 0);
    }
    local.Index = 0;
    check_private(kernel_scheduler_context_valid_on(UINT32_MAX) == 0);
    present = 0;
    check_private(kernel_scheduler_context_valid_on(0) == 0);
    present = 1;
    local.State = X86_PERCPU_PREPARED;
    check_private(kernel_scheduler_context_valid_on(0) == 0);
    local.State = X86_PERCPU_ACTIVE;
    local.Index = X86_PERCPU_CAPACITY;
    check_private(kernel_scheduler_context_valid_on(local.Index) == 0);
    local.Index = 0;
    dispatcher = 0;
    check_private(kernel_scheduler_context_valid_on(0) == 0);
    dispatcher = 2;
    check_private(kernel_scheduler_context_valid_on(0) == 0);
    dispatcher = 1;
    check_private(kernel_scheduler_context_valid_on(0) == 1);
    check_private(lock_calls == 0);
    printf("PASS: %u native CPU authority checks, CPU accessor and dispatcher MOCK\n",checks);
    return 0;
}
