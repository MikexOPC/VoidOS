#include "kernel/scheduler/scheduler.h"
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

#define CPUS 4U
#define TASKS 20000U

static SRWLOCK core_lock = SRWLOCK_INIT;
static volatile LONG running[KERNEL_SCHEDULER_CAPACITY];
static volatile LONG published, completed, preempted, failures;
static volatile LONG acquired, released;
static __declspec(thread) uint32_t local_cpu;
static __declspec(thread) uint32_t authority = 1;

// ====== backend_mocks ======

uint32_t kernel_scheduler_context_valid(void) { return 1; }
uint32_t kernel_scheduler_context_valid_on(uint32_t cpu) { return cpu == local_cpu ? authority : 0; }
uint32_t kernel_scheduler_binding_context_valid(void) { return 1; }
uint32_t kernel_scheduler_payload_context_valid(uint32_t consumer, uint64_t generation)
{ (void)consumer; (void)generation; return 1; }
__declspec(noreturn) void kernel_scheduler_fail_stop(void) { fprintf(stderr, "fail stop\n"); exit(2); }
uint32_t kernel_scheduler_lock_acquire(void)
{
    AcquireSRWLockExclusive(&core_lock);
    InterlockedIncrement(&acquired);
    return 1;
}
void kernel_scheduler_lock_release(void)
{
    InterlockedIncrement(&released);
    ReleaseSRWLockExclusive(&core_lock);
}

// ====== authority_private ======

static int authority_private(void)
{
    const uint32_t denied = KERNEL_SCHEDULER_DENIED;
    for (uint32_t value = 0; value <= 2; value += 2) {
        authority = value;
        if (kernel_scheduler_apply_on(0,KERNEL_SCHEDULER_RESERVE,KERNEL_SCHEDULER_NONE,0).status != denied ||
            kernel_scheduler_event_apply_on(0,0,0,1,0,0).reply.status != denied ||
            kernel_scheduler_stop_apply_on(0,0,0,1).reply.status != denied ||
            kernel_scheduler_tick_on(0).status != denied ||
            kernel_scheduler_affinity_on(0,0,1,1) != denied ||
            kernel_scheduler_configure(CPUS,2) != denied) return 0;
    }
    authority = 1;
    for (uint32_t cpu = 0; cpu < CPUS; ++cpu) {
        uint32_t other = (cpu + 1) % CPUS;
        KERNEL_SCHEDULER_REPLY task;
        KERNEL_EVENT_REPLY event;
        local_cpu = cpu;
        if (kernel_scheduler_apply_on(other,KERNEL_SCHEDULER_RESERVE,KERNEL_SCHEDULER_NONE,0).status != denied ||
            kernel_scheduler_event_apply_on(other,0,0,1,0,0).reply.status != denied ||
            kernel_scheduler_stop_apply_on(other,0,0,1).reply.status != denied ||
            kernel_scheduler_tick_on(other).status != denied ||
            kernel_scheduler_affinity_on(other,0,1,1) != denied) return 0;
        if (cpu && (kernel_scheduler_apply(KERNEL_SCHEDULER_RESERVE,KERNEL_SCHEDULER_NONE,0).status != denied ||
            kernel_scheduler_event_apply(0,0,1,0,0).reply.status != denied ||
            kernel_scheduler_stop_apply(0,0,1).reply.status != denied ||
            kernel_scheduler_irq_drain().reply.status != denied ||
            kernel_scheduler_configure(CPUS,2) != denied)) return 0;
        task = kernel_scheduler_apply_on(cpu,KERNEL_SCHEDULER_RESERVE,KERNEL_SCHEDULER_NONE,0);
        if (task.status || task.index != 0 || task.generation != cpu + 1) return 0;
        if (kernel_scheduler_affinity_on(cpu,task.index,task.generation,1ULL << cpu) ||
            kernel_scheduler_apply_on(cpu,KERNEL_SCHEDULER_PUBLISH,task.index,task.generation).status ||
            kernel_scheduler_apply_on(cpu,KERNEL_SCHEDULER_SELECT,KERNEL_SCHEDULER_NONE,0).status) return 0;
        event = kernel_scheduler_event_apply_on(cpu,0,task.index,task.generation,0,0);
        if (event.reply.status ||
            kernel_scheduler_event_apply_on(cpu,1,task.index,task.generation,0,event.epoch).reply.status) return 0;
        event = kernel_scheduler_stop_apply_on(cpu,0,task.index,task.generation);
        if (event.reply.status || event.woken != 1 ||
            kernel_scheduler_apply_on(cpu,KERNEL_SCHEDULER_SELECT,KERNEL_SCHEDULER_NONE,0).status ||
            kernel_scheduler_apply_on(cpu,KERNEL_SCHEDULER_EXIT,task.index,task.generation).status ||
            kernel_scheduler_apply_on(cpu,KERNEL_SCHEDULER_COLLECT,task.index,task.generation).status) return 0;
    }
    local_cpu = 0;
    return acquired == released;
}

// ====== fail ======

static void fail(const char *what, uint32_t status)
{
    fprintf(stderr, "SMP stress failure: %s (%u)\n", what, status);
    InterlockedIncrement(&failures);
}

// ====== publish_private ======

static void publish_private(uint32_t cpu, uint32_t round)
{
    KERNEL_SCHEDULER_REPLY task;
    if (published >= (LONG)TASKS) return;
    task = kernel_scheduler_apply_on(cpu, KERNEL_SCHEDULER_RESERVE, KERNEL_SCHEDULER_NONE, 0);
    if (task.status == KERNEL_SCHEDULER_FULL) return;
    if (task.status != KERNEL_SCHEDULER_OK) { fail("reserve", task.status); return; }
    if ((round / CPUS) % 3 == 0 &&
        kernel_scheduler_affinity_on(cpu, task.index, task.generation, 1ULL << ((round / CPUS) % CPUS)))
        fail("affinity", round);
    if (InterlockedIncrement(&published) > (LONG)TASKS) {
        InterlockedDecrement(&published);
        task = kernel_scheduler_apply_on(cpu, KERNEL_SCHEDULER_ABORT, task.index, task.generation);
        if (task.status != KERNEL_SCHEDULER_OK) fail("abort", task.status);
        return;
    }
    task = kernel_scheduler_apply_on(cpu, KERNEL_SCHEDULER_PUBLISH, task.index, task.generation);
    if (task.status != KERNEL_SCHEDULER_OK) fail("publish", task.status);
}

// ====== run_private ======

static void run_private(uint32_t cpu, KERNEL_SCHEDULER_REPLY task, uint32_t round)
{
    KERNEL_SCHEDULER_REPLY reply;
    if (InterlockedExchange(&running[task.index], 1) != 0) fail("task on two CPUs", task.index);
    for (uint32_t tick = 0; tick < (round / CPUS) % 3; ++tick) {
        reply = kernel_scheduler_tick_on(cpu);
        if (reply.status != KERNEL_SCHEDULER_OK) { fail("tick", reply.status); break; }
        if (reply.state == 1) {
            InterlockedExchange(&running[task.index], 0);
            reply = kernel_scheduler_apply_on(cpu, KERNEL_SCHEDULER_PREEMPT, task.index, task.generation);
            if (reply.status != KERNEL_SCHEDULER_OK) fail("preempt", reply.status);
            InterlockedIncrement(&preempted);
            return;
        }
    }
    InterlockedExchange(&running[task.index], 0);
    if ((round / CPUS) % 4 == 0) {
        reply = kernel_scheduler_apply_on(cpu, KERNEL_SCHEDULER_YIELD, task.index, task.generation);
        if (reply.status != KERNEL_SCHEDULER_OK) fail("yield", reply.status);
        return;
    }
    reply = kernel_scheduler_apply_on(cpu, KERNEL_SCHEDULER_EXIT, task.index, task.generation);
    if (reply.status != KERNEL_SCHEDULER_OK) { fail("exit", reply.status); return; }
    reply = kernel_scheduler_apply_on(cpu, KERNEL_SCHEDULER_COLLECT, task.index, task.generation);
    if (reply.status != KERNEL_SCHEDULER_OK) { fail("collect", reply.status); return; }
    InterlockedIncrement(&completed);
}

// ====== cpu_thread ======

static DWORD WINAPI cpu_thread(LPVOID argument)
{
    uint32_t cpu = (uint32_t)(uintptr_t)argument;
    local_cpu = cpu;
    for (uint32_t round = cpu; completed < (LONG)TASKS && !failures; round += CPUS) {
        KERNEL_SCHEDULER_REPLY task;
        publish_private(cpu, round);
        task = kernel_scheduler_apply_on(cpu, KERNEL_SCHEDULER_SELECT, KERNEL_SCHEDULER_NONE, 0);
        if (task.status == KERNEL_SCHEDULER_OK) run_private(cpu, task, round);
        else if (task.status != KERNEL_SCHEDULER_BLOCKED && task.status != KERNEL_SCHEDULER_EMPTY)
            fail("select", task.status);
    }
    return 0;
}

// ====== main ======

int main(void)
{
    HANDLE threads[CPUS];
    KERNEL_SCHEDULER_REPLY reply = kernel_scheduler_apply(KERNEL_SCHEDULER_INIT, KERNEL_SCHEDULER_NONE, 0);
    if (reply.status != KERNEL_SCHEDULER_OK || kernel_scheduler_configure(CPUS, 2) != KERNEL_SCHEDULER_OK ||
        kernel_scheduler_configure(0, 2) != KERNEL_SCHEDULER_BAD_CPU) return 1;
    if (kernel_scheduler_apply_on(CPUS, KERNEL_SCHEDULER_SELECT, KERNEL_SCHEDULER_NONE, 0).status !=
        KERNEL_SCHEDULER_DENIED || kernel_scheduler_tick_on(CPUS).status != KERNEL_SCHEDULER_DENIED) return 1;
    if (kernel_scheduler_apply(KERNEL_SCHEDULER_PREEMPT, 0, 1).status != KERNEL_SCHEDULER_BAD_ARGUMENT) return 1;
    if (!authority_private()) { fprintf(stderr,"CPU authority boundary failed\n"); return 1; }
    puts("SCHEDULER_CPU_AUTHORITY_PASS: wrong CPU, absent/non-normalized authority, BSP APIs and AP events");
    for (uint32_t cpu = 0; cpu < CPUS; ++cpu) {
        threads[cpu] = CreateThread(NULL, 0, cpu_thread, (LPVOID)(uintptr_t)cpu, 0, NULL);
        if (!threads[cpu]) return 1;
    }
    WaitForMultipleObjects(CPUS, threads, TRUE, INFINITE);
    for (uint32_t cpu = 0; cpu < CPUS; ++cpu) CloseHandle(threads[cpu]);
    if (failures || completed != (LONG)TASKS || published != (LONG)TASKS) return 3;
    for (uint32_t cpu = 0; cpu < CPUS; ++cpu) {
        local_cpu = cpu;
        if (kernel_scheduler_apply_on(cpu, KERNEL_SCHEDULER_SELECT, KERNEL_SCHEDULER_NONE, 0).status !=
            KERNEL_SCHEDULER_EMPTY) return 4;
    }
    if (acquired != released) return 6;
    printf("SCHEDULER_SMP_HOST_PASS: %u tasks on %u CPUs, %ld preemptions, no task on two CPUs\n",
        TASKS, CPUS, preempted);
    return preempted ? 0 : 5;
}
