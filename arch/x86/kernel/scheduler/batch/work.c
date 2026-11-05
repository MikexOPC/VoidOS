#include "batch.h"
#include "arch/x86/kernel/cpu/percpu/service/native/native.h"
int _fltused;

// ====== xat_yield ======

void xat_yield(void)
{
    X86_SCHEDULER_LOCAL *local = x86_scheduler_local_current();
    if (!local || !xat_task_valid() || local->event ||
        !x86_scheduler_context_resume_valid(&local->dispatcher,X86_AP_STACK_BASE(1),65536,
            X86_KERNEL_IMAGE_BASE,X86_KERNEL_IMAGE_LIMIT,xat_mask)) xat_abort();
    uint32_t index = local->current;
    uint64_t generation = local->generation;
    local->event = X86_TASK_YIELD;
    x86_scheduler_context_switch(&xat_contexts[index],&local->dispatcher);
    local = x86_scheduler_local_current();
    if (!xat_task_valid() || !local || local->current != index || local->generation != generation || local->event) xat_abort();
}

// ====== interrupt_window_private ======

// Intel SDM 078US §6.8.1: existing ks_irq_restore/ks_irq_save backend controls IF9.
static void interrupt_window_private(void)
{
    ks_irq_restore(512);
    for (uint32_t poll = 0; poll < 1024; ++poll) ks_pause();
    if (!(ks_irq_save() & 512)) xat_abort();
}

// ====== xat_task_start ======

__declspec(noreturn) void xat_task_start(void)
{
    X86_SCHEDULER_LOCAL *local = x86_scheduler_local_current();
    if (!local || !xat_task_valid()) xat_abort();
    uint32_t index = local->current, start, now;
    uint64_t checksum = 0;
    double floating = (double)(index + 1);
    if (!xis_native_now(NULL,&start)) xat_abort();
    for (uint32_t step = 1; step <= 16; ++step) {
        checksum += (uint64_t)(index + 1) * step;
        floating += (double)(index + 1);
        ks_atomic_store_release(&xat_record.Progress[index],step);
        interrupt_window_private();
        xat_yield();
    }
    uint32_t released = 0;
    for (uint32_t poll = 0; poll < 4096; ++poll) {
        if (xad_release(1,1,1,xat_record.Root,0)) { released = 1; break; }
        if (!xis_native_now(NULL,&now) || ((now - start) & xp_panic_native.Source.Mask) >= KERNEL_PM_TIMER_HZ)
            xat_abort();
        interrupt_window_private(); xat_yield();
    }
    local = x86_scheduler_local_current();
    if (!released || !local || !xat_task_valid() || checksum != 136ULL * (index + 1) ||
        floating != (double)(17 * (index + 1))) xat_abort();
    xat_record.Checksums[index] = checksum;
    xat_record.Floating[index] = (uint64_t)floating;
    if (!x86_scheduler_context_resume_valid(&local->dispatcher,X86_AP_STACK_BASE(1),65536,
        X86_KERNEL_IMAGE_BASE,X86_KERNEL_IMAGE_LIMIT,xat_mask)) xat_abort();
    local->event = X86_TASK_EXIT;
    x86_scheduler_context_switch(&xat_contexts[index],&local->dispatcher);
    xat_abort();
}
