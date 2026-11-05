#include "arch/x86/kernel/scheduler/batch/batch.h"
#include "arch/x86/kernel/scheduler/irq/bridge.h"
#include "arch/x86/kernel/cpu/percpu/service/native/native.h"
#include "kernel/sync/internal.h"
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

X86_TASK_CONTEXT xat_contexts[8];
uint32_t xat_mask;
XP_PANIC_NATIVE xp_panic_native;
const unsigned char x86_scheduler_irq_compute_begin[64] = {0};
const unsigned char x86_scheduler_irq_compute_end[1] = {0};
#ifdef KERNEL_TEST_AP_CONCURRENT
static XAT_PREEMPT peer_records[XAT_APS - 1];
static uint32_t task_offset;
XAT_PREEMPT *xat_preemption_record(void) { return task_offset ? &peer_records[task_offset / 2 - 1] : &xat_preempt; }
#define xat_preempt (*xat_preemption_record())
#else
#define task_offset 0U
#endif
static X86_SCHEDULER_LOCAL local;
static KERNEL_LOCK_CONTEXT locks;
static X86_IRQ_FRAME frame;
static uint32_t mmio[256], actor = 1, valid = 1, resume = 1, checks, switches;
static uint64_t irq;
static jmp_buf failure;

// ====== check_private ======

static void check_private(int value)
{ ++checks; if (!value) { fprintf(stderr,"AP preempt check %u\n",checks); exit(1); } }

// ====== mocked_hardware_and_switch ======

__declspec(noreturn) void xat_abort(void) { longjmp(failure,1); }
int xat_ap_actor(void) { return (int)actor; }
int xat_task_valid(void) { return (int)valid; }
uint64_t ks_irq_state(void) { return irq; }
X86_SCHEDULER_LOCAL *x86_scheduler_local_current(void) { return &local; }
KERNEL_LOCK_CONTEXT *kernel_lock_current_context(void) { return &locks; }
uint32_t xis_mmio_read(uint32_t offset)
{ check_private(offset < 1024 && !(offset & 3)); return mmio[offset / 4]; }
void xis_mmio_write(uint32_t offset, uint32_t value)
{
    check_private(offset < 1024 && !(offset & 3)); mmio[offset / 4] = value;
    if (offset == 0x380) mmio[0x390 / 4] = value;
    if (offset == 0xb0) mmio[0x160 / 4] = 0;
}
int x86_scheduler_context_resume_valid(const X86_TASK_CONTEXT *context, uint64_t base,
    uint64_t bytes, uint64_t begin, uint64_t end, uint32_t mask)
{ (void)context; (void)base; (void)bytes; (void)begin; (void)end; (void)mask; return (int)resume; }
void x86_scheduler_context_switch(X86_TASK_CONTEXT *previous, const X86_TASK_CONTEXT *next)
{
    check_private(previous == &xat_contexts[local.current] && next == &local.dispatcher);
    check_private(local.event == X86_TASK_PREEMPT);
    ++switches; local.event = 0;
}
int x86_scheduler_irq_poll(const volatile uint32_t *source, uint32_t port, uint32_t mask, uint32_t ticks)
{ (void)source; (void)port; (void)mask; (void)ticks; return 0; }

// ====== reset_private ======

static void reset_private(void)
{
    memset(&xat_preempt,0,sizeof(xat_preempt)); memset(&local,0,sizeof(local));
    memset(&locks,0,sizeof(locks)); memset(mmio,0,sizeof(mmio));
    memset(&frame,0,sizeof(frame)); actor = valid = resume = 1; irq = 0;
    local.current = task_offset; local.generation = 4; frame.Vector = XIS_TIMER_VECTOR; frame.Flags = 512;
    frame.Rip = (uint64_t)(uintptr_t)x86_scheduler_irq_compute_begin;
    mmio[0x160 / 4] = 1U << 17;
    mmio[0x3e0 / 4] = 3;
}

// ====== expect_interrupt_failure ======

static void expect_interrupt_failure(void)
{
    if (!setjmp(failure)) { xat_preempt_interrupt(&frame); check_private(0); }
    else check_private(1);
}

// ====== expect_resume_failure ======

static void expect_resume_failure(void)
{
    if (!setjmp(failure)) { xat_preempt_after(&frame); check_private(0); }
    else check_private(1);
}

// ====== test_cpu_private ======

static void test_cpu_private(void)
{
    reset_private(); check_private(!xat_preempt_quiet());
    actor = 0; expect_interrupt_failure(); reset_private();
    valid = 0; expect_interrupt_failure(); reset_private();
    local.current = XAT_TASKS; expect_interrupt_failure(); reset_private();
    frame.Vector = 0xe1; expect_interrupt_failure(); reset_private();
    irq = 512; expect_interrupt_failure(); reset_private();
    mmio[0x160 / 4] = 0; expect_interrupt_failure(); reset_private();
    local.current = task_offset + 1; expect_interrupt_failure(); reset_private();
    frame.Rip = 0; expect_interrupt_failure(); reset_private();
    for (uint32_t tick = 0; tick < 2 * XAT_PREEMPT_TARGET; ++tick) {
        local.current = task_offset + tick % 2; mmio[0x160 / 4] = 1U << 17;
        xat_preempt_interrupt(&frame);
        check_private(xat_preempt.Total == tick + 1 && !mmio[0x160 / 4]);
        locks.Depth = 1; expect_resume_failure(); locks.Depth = 0;
        resume = 0; expect_resume_failure(); resume = 1;
        local.event = 1; expect_resume_failure(); local.event = 0;
        frame.Flags = 0; expect_resume_failure(); frame.Flags = 512;
        xat_preempt_after(&frame);
        check_private(xat_preempt.Resumed[tick % 2] == tick / 2 + 1);
    }
    check_private(switches == 2 * XAT_PREEMPT_TARGET && xat_preempt_quiet());
    mmio[0x260 / 4] = 1U << 17; check_private(!xat_preempt_quiet()); mmio[0x260 / 4] = 0;
    for (uint32_t tick = 0; tick < 8; ++tick) {
        mmio[0x160 / 4] = 1U << 17; xat_preempt_interrupt(&frame); xat_preempt_after(&frame);
    }
    check_private(switches == 2 * XAT_PREEMPT_TARGET && xat_preempt.Total == 2 * XAT_PREEMPT_TARGET && xat_preempt.Late == 8);
    mmio[0x160 / 4] = 1U << 17; expect_interrupt_failure();
}

// ====== main ======

int main(void)
{
#ifdef KERNEL_TEST_AP_CONCURRENT
    for (task_offset = 0; task_offset < XAT_TASKS; task_offset += 2) { switches = 0; test_cpu_private(); }
#else
    test_cpu_private();
#endif
    printf("AP_PREEMPT_PASS: %u checks; real C policy, MMIO and switch MOCK\n",checks);
    return 0;
}
