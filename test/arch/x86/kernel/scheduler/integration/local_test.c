#include "arch/x86/kernel/scheduler/integration/local.h"
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include "kernel/sync/internal.h"

static uint32_t checks;
static X86_CPU_LOCAL owners[64];
static X86_SCHEDULER_LOCAL *slots[64];
static __declspec(thread) const X86_CPU_LOCAL *selected;
static HANDLE handoff_waiting;
static uint64_t sequence, payload[16];

// ====== ks_atomic_load_acquire ======

uint64_t ks_atomic_load_acquire(const uint64_t *value) {
    return (uint64_t)InterlockedCompareExchange64((volatile LONG64 *)value,0,0);
}

// ====== ks_atomic_load_relaxed ======

uint64_t ks_atomic_load_relaxed(const uint64_t *value) {
    return ks_atomic_load_acquire(value);
}

// ====== ks_atomic_store_release ======

void ks_atomic_store_release(uint64_t *value, uint64_t next) {
    (void)InterlockedExchange64((volatile LONG64 *)value,(LONG64)next);
}

// ====== ks_atomic_compare ======

uint32_t ks_atomic_compare(uint64_t *value, uint64_t expected, uint64_t next) {
    return (uint64_t)InterlockedCompareExchange64((volatile LONG64 *)value,(LONG64)next,(LONG64)expected) == expected;
}

// ====== ks_pause ======

void ks_pause(void) {
    if (handoff_waiting)
        (void)SetEvent(handoff_waiting);
    SwitchToThread();
}

// ====== mocked_deadline_start ======

int kernel_deadline_start(const KERNEL_CLOCK *clock, uint32_t ticks, KERNEL_DEADLINE *deadline) {
    if (!clock || !clock->Read || !clock->Read(clock->Context,&deadline->Start))
        return 0;
    deadline->Ticks = ticks;
    return 1;
}

// ====== mocked_deadline_expired ======

int kernel_deadline_expired(const KERNEL_CLOCK *clock, const KERNEL_DEADLINE *deadline) {
    uint32_t now;
    return !clock->Read(clock->Context,&now) || (uint32_t)(now - deadline->Start) >= deadline->Ticks;
}

// ====== host_clock_private ======

static int host_clock_private(void *context, uint32_t *value) {
    (void)context;
    *value = GetTickCount();
    return 1;
}

// ====== failed_clock_private ======

static int failed_clock_private(void *context, uint32_t *value) {
    (void)context;
    (void)value;
    return 0;
}

// ====== mocked_cpu_accessor ======

const X86_CPU_LOCAL *x86_percpu_current_local(void) { return selected; }

// ====== check_private ======

static void check_private(int condition)
{
    ++checks;
    if (!condition) { fprintf(stderr,"Local dispatcher check %u\n",checks); exit(1); }
}

// ====== worker_private ======

static DWORD WINAPI worker_private(void *argument)
{
    uint32_t cpu = (uint32_t)(uintptr_t)argument;
    selected = &owners[cpu];
    for (uint32_t step = 0; step < 100000; ++step) {
        X86_SCHEDULER_LOCAL *local = x86_scheduler_local_current();
        if (local != slots[cpu] || x86_scheduler_local_prepare() != local) return 1;
        local->generation = step + 1;
        local->stats.steps = step + 1;
        local->dispatcher.rbx = ((uint64_t)cpu << 32) | (step + 1);
        if (local->generation != step + 1 || local->stats.steps != step + 1 ||
            local->dispatcher.rbx != (((uint64_t)cpu << 32) | (step + 1))) return 2;
    }
    return 0;
}

// ====== binding_worker_private ======

static DWORD WINAPI binding_worker_private(void *argument) {
    uint32_t cpu = (uint32_t)(uintptr_t)argument;
    KERNEL_LOCK_WAIT wait = {{host_clock_private,NULL,UINT32_MAX,1000},10000,1000000};
    for (uint32_t step = 0; step < 1000; ++step) {
        if (!x86_scheduler_task_claim(1,cpu,&wait)) return 1;
        for (uint32_t word = 0; word < 16; ++word)
            if (payload[word] != sequence) return 2;
        ++sequence;
        for (uint32_t word = 0; word < 16; ++word)
            payload[word] = sequence;
        if (!x86_scheduler_task_release(1,cpu)) return 3;
    }
    return 0;
}

// ====== handoff_worker_private ======

static DWORD WINAPI handoff_worker_private(void *argument) {
    KERNEL_LOCK_WAIT wait = {{host_clock_private,NULL,UINT32_MAX,1000},10000,1000000};
    (void)argument;
    if (!x86_scheduler_task_claim(0,1,&wait)) return 1;
    if (payload[0] != 0x123456789abcdef0ULL) return 2;
    return x86_scheduler_task_release(0,1) ? 0 : 3;
}

// ====== binding_checks_private ======

static void binding_checks_private(void) {
    DWORD code;
    HANDLE threads[2];
    KERNEL_LOCK_WAIT failed = {{failed_clock_private,NULL,UINT32_MAX,1000},1,1};
    check_private(!x86_scheduler_task_binding(8));
    check_private(!x86_scheduler_task_binding_init(8,3,0));
    check_private(!x86_scheduler_task_binding_init(0,0,0));
    check_private(!x86_scheduler_task_binding_init(0,3,1));
    check_private(x86_scheduler_task_binding_init(0,3,0));
    check_private(x86_scheduler_task_claim(0,0,NULL));
    check_private(!x86_scheduler_task_claim(0,0,NULL));
    check_private(!x86_scheduler_task_claim(0,2,NULL));
    check_private(!x86_scheduler_task_claim(0,1,&failed));
    check_private(!x86_scheduler_task_release(0,1));
    check_private(!x86_scheduler_task_binding_init(0,1,1));
    handoff_waiting = CreateEventW(NULL,TRUE,FALSE,NULL);
    check_private(handoff_waiting != NULL);
    threads[0] = CreateThread(NULL,0,handoff_worker_private,NULL,0,NULL);
    check_private(threads[0] != NULL);
    check_private(WaitForSingleObject(handoff_waiting,10000) == WAIT_OBJECT_0);
    check_private(ks_atomic_load_acquire(&x86_scheduler_task_binding(0)->Owner) == 1);
    payload[0] = 0x123456789abcdef0ULL;
    check_private(x86_scheduler_task_release(0,0));
    check_private(WaitForSingleObject(threads[0],10000) == WAIT_OBJECT_0);
    check_private(GetExitCodeThread(threads[0],&code) && !code);
    check_private(CloseHandle(threads[0]) && CloseHandle(handoff_waiting));
    handoff_waiting = NULL;
    X86_TASK_BINDING *binding = x86_scheduler_task_binding(0);
    check_private(!binding->Owner && binding->Visits == 3 && binding->Migrations == 1 && binding->Dispatches == 2);
    check_private(x86_scheduler_task_binding_init(1,3,0));
    for (uint32_t word = 0; word < 16; ++word)
        payload[word] = 0;
    for (uint32_t cpu = 0; cpu < 2; ++cpu) {
        threads[cpu] = CreateThread(NULL,0,binding_worker_private,(void *)(uintptr_t)cpu,0,NULL);
        check_private(threads[cpu] != NULL);
    }
    check_private(WaitForMultipleObjects(2,threads,TRUE,10000) == WAIT_OBJECT_0);
    for (uint32_t cpu = 0; cpu < 2; ++cpu) {
        check_private(GetExitCodeThread(threads[cpu],&code) && !code);
        check_private(CloseHandle(threads[cpu]));
    }
    binding = x86_scheduler_task_binding(1);
    check_private(!binding->Owner && binding->Visits == 3 && binding->Migrations > 0 && binding->Dispatches == 2000);
    check_private(sequence == 2000);
    puts("PASS: real native ownership handoff and 2000 host acquisitions; CPU accessor and deadline helpers mocked, host atomics");
}

// ====== main ======

int main(void)
{
    X86_CPU_LOCAL impostor = {0};
    HANDLE threads[4];
    binding_checks_private();
    check_private(!x86_scheduler_local_current() && !x86_scheduler_local_prepare());
    for (uint32_t cpu = 0; cpu < 64; ++cpu) {
        owners[cpu].Self = (uint64_t)(uintptr_t)&owners[cpu];
        owners[cpu].Index = cpu; owners[cpu].State = X86_PERCPU_ACTIVE;
        selected = &owners[cpu];
        check_private(!x86_scheduler_local_current());
        slots[cpu] = x86_scheduler_local_prepare();
        check_private(slots[cpu] && slots[cpu] == x86_scheduler_local_current());
        check_private(!((uintptr_t)slots[cpu] % 64));
        check_private(slots[cpu]->current == KERNEL_SCHEDULER_NONE && !slots[cpu]->generation &&
            !slots[cpu]->phase && !slots[cpu]->event && !slots[cpu]->stats.steps);
        for (uint32_t other = 0; other < cpu; ++other) {
            uintptr_t a = (uintptr_t)slots[cpu], b = (uintptr_t)slots[other];
            check_private(a + sizeof(*slots[cpu]) <= b || b + sizeof(*slots[other]) <= a);
        }
        slots[cpu]->phase = cpu % 4;
        slots[cpu]->dispatcher.rsp = cpu + 1;
        check_private(x86_scheduler_local_prepare() == slots[cpu] &&
            slots[cpu]->phase == cpu % 4 && slots[cpu]->dispatcher.rsp == cpu + 1);
    }
    selected = &impostor;
    impostor.Self = (uint64_t)(uintptr_t)&impostor;
    impostor.State = X86_PERCPU_ACTIVE;
    for (uint32_t cpu = 0; cpu < 64; ++cpu) {
        impostor.Index = cpu;
        check_private(!x86_scheduler_local_current() && !x86_scheduler_local_prepare());
    }
    selected = &owners[0];
    owners[0].State = X86_PERCPU_PREPARED;
    check_private(!x86_scheduler_local_prepare() && !x86_scheduler_local_current());
    owners[0].State = X86_PERCPU_ACTIVE;
    owners[0].Self = 0;
    check_private(!x86_scheduler_local_prepare() && !x86_scheduler_local_current());
    owners[0].Self = (uint64_t)(uintptr_t)&owners[0];
    owners[0].Index = 64;
    check_private(!x86_scheduler_local_prepare() && !x86_scheduler_local_current());
    owners[0].Index = UINT32_MAX;
    check_private(!x86_scheduler_local_prepare() && !x86_scheduler_local_current());
    owners[0].Index = 0;
    for (uint32_t cpu = 0; cpu < 4; ++cpu) {
        threads[cpu] = CreateThread(NULL,0,worker_private,(void *)(uintptr_t)cpu,0,NULL);
        check_private(threads[cpu] != NULL);
    }
    check_private(WaitForMultipleObjects(4,threads,TRUE,30000) == WAIT_OBJECT_0);
    for (uint32_t cpu = 0; cpu < 4; ++cpu) {
        DWORD code = 99;
        check_private(GetExitCodeThread(threads[cpu],&code) && !code);
        check_private(CloseHandle(threads[cpu]));
        check_private(slots[cpu]->generation == 100000 && slots[cpu]->stats.steps == 100000);
    }
    for (uint32_t cpu = 0; cpu < 64; ++cpu) {
        selected = &owners[cpu];
        check_private(x86_scheduler_local_current() == slots[cpu] &&
            slots[cpu]->phase == cpu % 4 && slots[cpu]->dispatcher.rsp == cpu + 1);
    }
    printf("PASS: %u dispatcher storage checks; 400000 updates on four host threads; %zu bytes/CPU; accessor MOCK\n",
        checks,sizeof(X86_SCHEDULER_LOCAL));
    return 0;
}
