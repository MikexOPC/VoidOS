#include "kernel/sync/internal.h"
#include <intrin.h>
#ifdef PHYSICAL_THREAD_TEST
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#define CPU_LOCAL __declspec(thread)
#else
#define CPU_LOCAL
#endif
static CPU_LOCAL KERNEL_LOCK_CONTEXT cpu_context;
static CPU_LOCAL uint64_t simulated_if = 0x200;
static CPU_LOCAL int context_allowed = 1;

// ====== physical_host_cpu_model ======

#ifdef PHYSICAL_THREAD_TEST
static int read_clock(void *state, uint32_t *value) {
    (void)state;
    *value = GetTickCount();
    return 1;
}
static const KERNEL_LOCK_WAIT wait = {{read_clock, NULL, UINT32_MAX, 1000}, 5000, 100000000};
#endif

// ====== kernel_lock_current_context ======

KERNEL_LOCK_CONTEXT *kernel_lock_current_context(void) {
    if (!context_allowed) return NULL;
    if (!cpu_context.Self) {
#ifdef PHYSICAL_THREAD_TEST
        if (!kernel_lock_context_initialize(&cpu_context, &wait)) __fastfail(7);
#else
        if (!kernel_lock_context_initialize(&cpu_context, NULL)) __fastfail(7);
#endif
    }
    return ks_context_valid(&cpu_context) ? &cpu_context : NULL;
}

// ====== simulated_irq_backend ======

uint64_t ks_irq_save(void) { uint64_t saved = simulated_if; simulated_if = 0; return saved; }
uint64_t ks_irq_state(void) { return simulated_if; }
void ks_irq_restore(uint64_t value) { if (value & ~0x200ULL) __fastfail(7); simulated_if = value; }
void ks_pause(void) { _mm_pause(); }
__declspec(noreturn) void ks_fail_stop(const char *reason) {
#ifdef PHYSICAL_THREAD_TEST
    fprintf(stderr, "Physical host fail-stop: %s\n", reason);
    fflush(stderr);
#else
    (void)reason;
#endif
    __fastfail(7);
}

// ====== physical_test_context_allowed ======

void physical_test_context_allowed(int allowed) { context_allowed = allowed; }
