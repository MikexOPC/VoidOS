#include "arch/x86/kernel/scheduler/batch/batch.h"
#include "arch/x86/kernel/cpu/percpu/service/native/native.h"
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>

XAT_RECORD xat_record;
XAT_PREEMPT xat_preempt;
XP_PANIC_NATIVE xp_panic_native;
extern uint64_t xat_arrived[2];
static uint32_t actor = 1, valid = 1, clock_mode, polls, checks;
static jmp_buf failure;

// ====== check_private ======

static void check_private(int value)
{ ++checks; if (!value) { fprintf(stderr,"Concurrent barrier check %u\n",checks); exit(1); } }

// ====== backend_mocks ======

__declspec(noreturn) void xat_abort(void) { longjmp(failure,1); }
uint32_t xis_native_index(void) { return actor; }
int xat_task_valid(void) { return (int)valid; }
uint64_t ks_atomic_load_acquire(const uint64_t *value) { return *value; }
void ks_atomic_store_release(uint64_t *value, uint64_t next) { *value = next; }
int xis_native_now(void *context, uint32_t *value)
{
    (void)context;
    *value = polls++ && clock_mode == 2 ? KERNEL_PM_TIMER_HZ : 0;
    return clock_mode != 1;
}
void ks_pause(void) { xat_arrived[2 - actor] = 1; }

// ====== expect_failure ======

static void expect_failure(uint32_t index)
{
    if (!setjmp(failure)) { xat_concurrent_barrier(index); check_private(0); }
    else check_private(1);
}

// ====== main ======

int main(void)
{
    xp_panic_native.Source.Mask = 0xffffff;
    for (actor = 1; actor <= 2; ++actor) {
        uint32_t index = (actor - 1) * 2;
        check_private(xat_cpu_record() == (actor == 1 ? &xat_record : &xat_second_record));
        check_private(xat_preemption_record() == (actor == 1 ? &xat_preempt : &xat_preempt_second));
        xat_arrived[0] = xat_arrived[1] = 0; polls = 0;
        xat_concurrent_barrier(index);
        check_private(xat_arrived[0] == 1 && xat_arrived[1] == 1);
        expect_failure(index);
        expect_failure(index ^ 2);
        expect_failure(4);
        xat_arrived[0] = xat_arrived[1] = 0;
        xat_concurrent_barrier(index + 1);
        check_private(!xat_arrived[0] && !xat_arrived[1]);
        valid = 0; expect_failure(index); valid = 1;
        clock_mode = 1; expect_failure(index);
        clock_mode = 2; polls = 0; expect_failure(index);
        clock_mode = 0;
    }
    for (actor = 0; actor <= 3; actor += 3) {
        if (!setjmp(failure)) { (void)xat_cpu_record(); check_private(0); } else check_private(1);
        if (!setjmp(failure)) { (void)xat_preemption_record(); check_private(0); } else check_private(1);
    }
    printf("AP_CONCURRENT_BARRIER_PASS: %u checks; actual C helpers, CPU/clock/atomics MOCK\n",checks);
    return 0;
}
