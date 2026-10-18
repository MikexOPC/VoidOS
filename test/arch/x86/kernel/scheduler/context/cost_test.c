#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include "arch/x86/kernel/scheduler/context.h"

// Host-only measurement (B6): cost of one dispatcher->task->dispatcher round trip through the
// register-pattern probe (diagnostic) versus the raw context switch (production). Real CPU, real
// assembly; numbers are informational, nothing is asserted on speed.
#define ROUND_TRIPS 500000U
#define SAMPLES 7U
static X86_TASK_CONTEXT actor, dispatcher;
static unsigned char *reservation;
static volatile uint32_t use_probe, running;
extern int x86_scheduler_probe(X86_TASK_CONTEXT *, const X86_TASK_CONTEXT *, uint64_t);

// ====== x86_scheduler_stack_authority ======

int x86_scheduler_stack_authority(const X86_TASK_CONTEXT *context, uint64_t base, uint64_t bytes)
{
    return context == &actor && base == (uint64_t)(uintptr_t)(reservation + 4096) && bytes == X86_TASK_STACK_BYTES;
}

// ====== x86_scheduler_task_start ======

// No Windows APIs on the alternate stack.
void x86_scheduler_task_start(void)
{
    for (;;) {
        if (use_probe) { if (!x86_scheduler_probe(&actor, &dispatcher, 0x1001)) running = 0; }
        else x86_scheduler_context_switch(&actor, &dispatcher);
    }
}

// ====== sample_private ======

static double sample_private(uint32_t probe)
{
    LARGE_INTEGER start, end, frequency;
    uint32_t index;
    use_probe = probe;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&start);
    for (index = 0; index < ROUND_TRIPS; ++index) {
        if (probe) { if (!x86_scheduler_probe(&dispatcher, &actor, 0x2003)) running = 0; }
        else x86_scheduler_context_switch(&dispatcher, &actor);
    }
    QueryPerformanceCounter(&end);
    if (!running) { fprintf(stderr, "Probe reported a preservation failure\n"); exit(1); }
    return (double)(end.QuadPart - start.QuadPart) * 1e9 / (double)frequency.QuadPart / ROUND_TRIPS;
}

// ====== compare_private ======

static int compare_private(const void *left, const void *right)
{
    double a = *(const double *)left, b = *(const double *)right;
    return a < b ? -1 : a > b;
}

// ====== main ======

int main(void)
{
    double raw[SAMPLES], probed[SAMPLES];
    uint32_t index;
    reservation = VirtualAlloc(NULL, (SIZE_T)X86_TASK_STACK_BYTES + 8192, MEM_RESERVE, PAGE_NOACCESS);
    if (!reservation || VirtualAlloc(reservation + 4096, X86_TASK_STACK_BYTES, MEM_COMMIT, PAGE_READWRITE) != reservation + 4096 ||
        !x86_scheduler_context_prepare(&actor, (uint64_t)(uintptr_t)(reservation + 4096), X86_TASK_STACK_BYTES)) return 1;
    running = 1;
    (void)sample_private(0); (void)sample_private(1); // Warm-up: first touch of the stack and caches.
    for (index = 0; index < SAMPLES; ++index) { raw[index] = sample_private(0); probed[index] = sample_private(1); }
    qsort(raw, SAMPLES, sizeof(raw[0]), compare_private);
    qsort(probed, SAMPLES, sizeof(probed[0]), compare_private);
    printf("PASS: %u round trips x %u samples; median raw switch %.1f ns, probe %.1f ns (+%.1f ns, x%.2f) per "
           "round trip of 2 switches; min raw %.1f, min probe %.1f (host only, informational)\n",
           ROUND_TRIPS, SAMPLES, raw[SAMPLES / 2], probed[SAMPLES / 2], probed[SAMPLES / 2] - raw[SAMPLES / 2],
           probed[SAMPLES / 2] / raw[SAMPLES / 2], raw[0], probed[0]);
    return 0;
}
