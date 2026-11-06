#include "batch.h"
#include "arch/x86/kernel/cpu/percpu/service/native/native.h"
int _fltused;
#ifdef KERNEL_TEST_AP_CONSOLE
#include "test/kernel/console/video/ap/channel.h"
#endif

// ====== interrupt_window_private ======

// Intel SDM 078US §6.8.1: existing ks_irq_restore/ks_irq_save backend controls IF9.
static void interrupt_window_private(void)
{
    ks_irq_restore(512);
    for (uint32_t poll = 0; poll < 1024; ++poll) ks_pause();
    if (!(ks_irq_save() & 512)) xat_abort();
}

// ====== xat_io_yield ======

#ifdef KERNEL_TEST_AP_CONSOLE
void xat_io_yield(void) { interrupt_window_private(); x86_scheduler_yield(); }
#endif

// ====== xat_task_body ======

void xat_task_body(uint32_t index)
{
    X86_SCHEDULER_LOCAL *local = x86_scheduler_local_current();
    if (!local || !xat_task_valid() || local->current != index) xat_abort();
    uint32_t start, now;
    uint64_t checksum = 0;
    double floating = (double)(index + 1);
    if (!xis_native_now(NULL,&start)) xat_abort();
    for (uint32_t step = 1; step <= 16; ++step) {
        checksum += (uint64_t)(index + 1) * step;
        floating += (double)(index + 1);
        ks_atomic_store_release(&xat_record.Progress[index],step);
#ifndef KERNEL_TEST_AP_PREEMPTION
        interrupt_window_private();
        x86_scheduler_yield();
#endif
    }
#ifdef KERNEL_TEST_AP_CONSOLE
    kca_produce(index);
    if (!xis_native_now(NULL,&start)) xat_abort();
#endif
#ifdef KERNEL_TEST_AP_PREEMPTION
#ifdef KERNEL_TEST_AP_CONCURRENT
    xat_concurrent_barrier(index);
#endif
    xat_preempt_work(index);
#endif
    uint32_t released = 0;
    for (uint32_t poll = 0; poll < 4096; ++poll) {
        if (xad_release(XAT_CPU(index),XAT_CPU(index),1,xat_record.Root,0)) { released = 1; break; }
        if (!xis_native_now(NULL,&now) || ((now - start) & xp_panic_native.Source.Mask) >= KERNEL_PM_TIMER_HZ)
            xat_abort();
        interrupt_window_private(); x86_scheduler_yield();
    }
    local = x86_scheduler_local_current();
    if (!released || !local || !xat_task_valid() || checksum != 136ULL * (index + 1) ||
        floating != (double)(17 * (index + 1))) xat_abort();
    xat_record.Checksums[index] = checksum;
    xat_record.Floating[index] = (uint64_t)floating;
}
