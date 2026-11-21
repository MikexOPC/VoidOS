#include "kernel/scheduler/scheduler.h"
#include "arch/x86/kernel/cpu/percpu/percpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static X86_CPU_LOCAL local;
static uint32_t present = 1, dispatcher = 1, checks, lock_calls;
static uint32_t native_checks;

// ====== backend_mocks ======

const X86_CPU_LOCAL *x86_percpu_current_local(void) { return present ? &local : NULL; }
KERNEL_LOCK_CONTEXT *x86_percpu_current_context(void) { return &local.Locks; }

// ====== kernel_scheduler_context_valid ======

uint32_t kernel_scheduler_context_valid(void) {
    ++native_checks;
    return dispatcher;
}

// ====== xp_gs_read ======

uint64_t xp_gs_read(void) {
    return present ? (uintptr_t)&local : 0;
}

// ====== xp_gs_self ======

uint64_t xp_gs_self(void) {
    return (uintptr_t)&local;
}

// ====== ks_irq_state ======

uint64_t ks_irq_state(void) {
    return 0;
}

// ====== xp_image_contains ======

int xp_image_contains(uintptr_t address, size_t bytes) {
    return address == (uintptr_t)&local && bytes == sizeof(local);
}
__declspec(noreturn) void kernel_scheduler_fail_stop(void) { exit(99); }

// ====== lock_mocks ======

int kernel_lock_initialize(KERNEL_LOCK *lock, uint32_t rank)
{
    ++lock_calls;
    lock->Self = (uintptr_t)lock;
    lock->Rank = rank;
    lock->Ready = 1;
    return 1;
}

int kernel_lock_acquire(KERNEL_LOCK *lock, KERNEL_LOCK_CONTEXT *context, KERNEL_LOCK_GUARD *guard)
{
    ++lock_calls;
    if (!native_checks || context->Depth || dispatcher != 1)
        return 0;
    *guard = (KERNEL_LOCK_GUARD){(uintptr_t)guard,context,lock,1,1,0};
    context->Frames[0] = (KERNEL_LOCK_FRAME){(uintptr_t)lock,(uintptr_t)guard,1,1,0,900,0};
    context->Depth = 1;
    return 1;
}

int kernel_lock_release(KERNEL_LOCK_CONTEXT *context, KERNEL_LOCK_GUARD *guard)
{
    ++lock_calls;
    context->Depth = 0;
    memset(context->Frames,0,sizeof(context->Frames));
    memset(guard,0,sizeof(*guard));
    return 1;
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
    local.Self = (uintptr_t)&local;
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
    dispatcher = 0;
    check_private(kernel_scheduler_lock_acquire() == 0);
    check_private(lock_calls == 0);
    dispatcher = 1;
    uint32_t before = native_checks;
    check_private(kernel_scheduler_lock_acquire() == 1);
    check_private(native_checks == before + 1);
    before = native_checks;
    check_private(kernel_scheduler_context_valid_on(0) == 1);
    check_private(kernel_scheduler_context_valid_on(1) == 0);
    check_private(native_checks == before);
    kernel_scheduler_lock_release();
    check_private(native_checks == before && !local.Locks.Depth);
    for (uint32_t cpu = 1; cpu < X86_PERCPU_CAPACITY; ++cpu) {
        local.Index = cpu;
        before = native_checks;
        check_private(kernel_scheduler_lock_acquire() == 1);
        check_private(native_checks == before + 1);
        before = native_checks;
        check_private(kernel_scheduler_context_valid_on(cpu) == 1);
        check_private(kernel_scheduler_context_valid_on((cpu + 1) % X86_PERCPU_CAPACITY) == 0);
        kernel_scheduler_lock_release();
        check_private(native_checks == before && !local.Locks.Depth);
    }
    printf("PASS: %u native CPU authority checks, CPU accessor and dispatcher MOCK\n",checks);
    return 0;
}
