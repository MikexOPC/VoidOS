#include "arch/x86/kernel/scheduler/integration/local.h"
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

static uint32_t checks;
static X86_CPU_LOCAL owners[64];
static X86_SCHEDULER_LOCAL *slots[64];
static __declspec(thread) const X86_CPU_LOCAL *selected;

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

// ====== main ======

int main(void)
{
    X86_CPU_LOCAL impostor = {0};
    HANDLE threads[4];
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
