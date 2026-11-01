#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <intrin.h>
#include "test.h"
static __declspec(thread) uint64_t fake_if = 0x200;
static void (*pause_hook)(void);
static KERNEL_LOCK *fatal_lock;

// ====== ks_irq_save ======

uint64_t ks_irq_save(void) {
    uint64_t saved = fake_if;
    fake_if = 0;
    return saved;
}

// ====== ks_irq_state ======

uint64_t ks_irq_state(void) { return fake_if; }

// ====== ks_irq_restore ======

void ks_irq_restore(uint64_t saved_if) { assert(!(saved_if & ~0x200ULL)); fake_if = saved_if; }

// ====== ks_pause ======

void ks_pause(void) { _mm_pause(); if (pause_hook) pause_hook(); }

// ====== test_set_if ======

void test_set_if(uint64_t value) { fake_if = value; }

// ====== test_set_pause ======

void test_set_pause(void (*hook)(void)) { pause_hook = hook; }

// ====== test_watch_fatal ======

void test_watch_fatal(KERNEL_LOCK *lock) { fatal_lock = lock; }

// ====== ks_fail_stop ======

__declspec(noreturn) void ks_fail_stop(const char *reason) {
    printf("EXPECTED_FAIL_STOP: %s", reason);
    if (fatal_lock) printf("; next=%llu serving=%llu", ks_atomic_load_relaxed(&fatal_lock->Next.Value),
        ks_atomic_load_acquire(&fatal_lock->Serving.Value));
    puts("");
    fflush(stdout);
    ExitProcess(86);
}

// ====== test_clock ======

int test_clock(void *context, uint32_t *value) {
    (void)context;
    *value = GetTickCount();
    return 1;
}
