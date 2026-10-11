#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include "arch/x86/kernel/scheduler/context.h"

static X86_TASK_CONTEXT actors[2], dispatcher;
static unsigned char *reservations[2];
static uint32_t selected, progress[2], finished[2], fault;
static uint32_t allocations, releases, switches;
extern int x86_scheduler_probe(X86_TASK_CONTEXT *, const X86_TASK_CONTEXT *, uint64_t);

// ====== check_private ======

static void check_private(int condition)
{
    if (!condition) { fprintf(stderr, "Switch check failed (%u switches)\n", switches); exit(1); }
}

// ====== guard_fault_private ======

// SEH only on the original Windows stack; never across our switch boundary.
static int guard_fault_private(volatile unsigned char *address)
{
    __try {
        *address = 0xA5;
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ?
                EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        return 1;
    }
    return 0;
}

// ====== x86_scheduler_stack_authority ======

int x86_scheduler_stack_authority(const X86_TASK_CONTEXT *context, uint64_t base, uint64_t bytes)
{
    uint32_t id;
    MEMORY_BASIC_INFORMATION low, middle, high;
    for (id = 0; id < 2; ++id) {
        if (context != &actors[id] || base != (uint64_t)(uintptr_t)(reservations[id] + 4096) ||
            bytes != X86_TASK_STACK_BYTES) continue;
        if (!VirtualQuery(reservations[id], &low, sizeof(low)) ||
            !VirtualQuery((void *)(uintptr_t)base, &middle, sizeof(middle)) ||
            !VirtualQuery((void *)(uintptr_t)(base + bytes), &high, sizeof(high))) return 0;
        return low.State == MEM_RESERVE && high.State == MEM_RESERVE &&
            middle.State == MEM_COMMIT && middle.Protect == PAGE_READWRITE &&
            middle.RegionSize == bytes && low.AllocationBase == reservations[id] &&
            high.AllocationBase == reservations[id] && middle.AllocationBase == reservations[id];
    }
    return 0;
}

// ====== x86_scheduler_task_start ======

// No Windows APIs, TLS, unwinding or sanitizer runtime on alternate stacks.
void x86_scheduler_task_start(void)
{
    uint32_t id = selected, iteration;
    volatile uint64_t canary[8];
    for (iteration = 0; iteration < 8; ++iteration) canary[iteration] = UINT64_C(0xABCD0000) + id + iteration;
    for (iteration = 0; iteration < 1000; ++iteration) {
        ++progress[id];
        if (!x86_scheduler_probe(&actors[id], &dispatcher, UINT64_C(0x1001) + id)) fault = 1;
        if (canary[iteration % 8] != UINT64_C(0xABCD0000) + id + iteration % 8) fault = 1;
    }
    finished[id] = 1;
    x86_scheduler_context_switch(&actors[id], &dispatcher);
    fault = 1; // Finished tasks must never be resumed.
    for (;;) x86_scheduler_context_switch(&actors[id], &dispatcher);
}

// ====== main ======

int main(void)
{
    uint32_t id, round;
    SYSTEM_INFO system;
    GetSystemInfo(&system);
    check_private(system.dwPageSize == 4096);
    for (id = 0; id < 2; ++id) {
        reservations[id] = VirtualAlloc(NULL, (SIZE_T)X86_TASK_STACK_BYTES + 8192,
                                        MEM_RESERVE, PAGE_NOACCESS);
        check_private(reservations[id] != NULL);
        ++allocations;
        check_private(VirtualAlloc(reservations[id] + 4096, (SIZE_T)X86_TASK_STACK_BYTES,
                                   MEM_COMMIT, PAGE_READWRITE) == reservations[id] + 4096);
        check_private(guard_fault_private(reservations[id] + 4095));
        check_private(guard_fault_private(reservations[id] + 4096 + X86_TASK_STACK_BYTES));
        check_private(x86_scheduler_context_prepare(&actors[id],
                      (uint64_t)(uintptr_t)(reservations[id] + 4096), X86_TASK_STACK_BYTES));
    }
    for (round = 0; round < 1001; ++round) {
        for (id = 0; id < 2; ++id) {
            selected = id;
            check_private(x86_scheduler_probe(&dispatcher, &actors[id], UINT64_C(0x2003)));
            switches += 2;
            check_private(!fault && progress[id] == (round < 1000 ? round + 1 : 1000));
            check_private(actors[id].rsp >= (uint64_t)(uintptr_t)(reservations[id] + 4096));
            check_private(actors[id].rsp < (uint64_t)(uintptr_t)(reservations[id] + 4096) + X86_TASK_STACK_BYTES);
        }
    }
    for (id = 0; id < 2; ++id) {
        check_private(finished[id] && progress[id] == 1000);
        check_private(VirtualFree(reservations[id], 0, MEM_RELEASE));
        ++releases;
    }
    check_private(allocations == releases);
    printf("PASS: %u real switches, GPR/XMM/FP/canaries, 4 guard faults; %u reservations released (host only)\n",
           switches, releases);
    return 0;
}
