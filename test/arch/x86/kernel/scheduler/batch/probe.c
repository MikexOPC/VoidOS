#include "arch/x86/kernel/scheduler/batch/batch.h"
#include "arch/x86/kernel/cpu/percpu/service/native/native.h"
#include "kernel/diagnostics/log.h"
#include "kernel/scheduler/lot.h"
#ifdef KERNEL_TEST_AP_CONCURRENT
extern uint64_t xat_arrived[2];
static KERNEL_LOT_REPLY lot_final;
#endif

// ====== request_private ======

static void request_private(uint32_t index, uint32_t idle)
{
    uint64_t token = xis_publish(index,0);
    XIS_WAIT wait = {xis_native_now,NULL,xp_panic_native.Source.Mask,KERNEL_PM_TIMER_HZ,1000000,0};
    if (!token || token == XIS_INVALID || !xis_native_send(index) ||
        xis_wait(index,token,&wait) != XIS_WAIT_ACK || (idle && !xis_native_wait(index,1,token))) xat_abort();
}

// ====== wait_private ======

static void wait_private(uint32_t cpu, uint64_t phase, uint32_t progress)
{
    uint32_t start, now;
    if (!xis_native_now(NULL,&start)) xat_abort();
    for (uint32_t poll = 0; poll < 1000000; ++poll) {
        uint64_t value = xad_value(cpu,x86_kernel_root());
        if (value == (1ULL << 3 | phase) && (!progress ||
            (ks_atomic_load_acquire(&xat_record.Progress[(cpu - 1) * 2]) && ks_atomic_load_acquire(&xat_record.Progress[(cpu - 1) * 2 + 1])))) return;
        if ((value & 7) == XAD_FAILED || !xis_native_now(NULL,&now) ||
            ((now - start) & xp_panic_native.Source.Mask) >= KERNEL_PM_TIMER_HZ) break;
        ks_pause();
    }
    xat_abort();
}

// ====== xat_batch_probe ======

__declspec(noreturn) void xat_batch_probe(void)
{
    uint64_t root = x86_kernel_root();
    if (xis_native_index() || ks_irq_state() || !xad_configure(xis_native.Count,root,0)) xat_abort();
    for (uint32_t index = 1; index < xis_native.Count; ++index) request_private(index,1);
    for (uint32_t cpu = 1; cpu <= XAT_APS; ++cpu) {
        if (cpu >= xis_native.Count || !xad_offer(cpu,0,1,root)) xat_abort();
        request_private(cpu,1); wait_private(cpu,XAD_ADMITTED,0);
    }
    xat_prepare();
#ifdef KERNEL_TEST_AP_PREEMPTION
    xat_preempt.Self = (uint64_t)(uintptr_t)&xat_preempt;
    xat_preempt.Reload = x86_timer_diagnostic_reload();
    if (!xat_preempt.Reload) xat_abort();
#ifdef KERNEL_TEST_AP_CONCURRENT
    xat_preempt_second.Self = (uint64_t)(uintptr_t)&xat_preempt_second;
    xat_preempt_second.Reload = xat_preempt.Reload;
#endif
#endif
#ifdef KERNEL_TEST_AP_CONCURRENT
    if (kernel_scheduler_lot_on(0,KERNEL_LOT_OPEN,root,1,6).status) xat_abort();
#endif
    for (uint32_t cpu = 1; cpu <= XAT_APS; ++cpu)
        if (!xad_transition(cpu,0,1,root,0)) xat_abort();
    for (uint32_t cpu = 1; cpu <= XAT_APS; ++cpu) request_private(cpu,0);
    for (uint32_t cpu = 1; cpu <= XAT_APS; ++cpu) wait_private(cpu,XAD_RUNNING,1);
    for (uint32_t round = 0; round < XAT_REQUEST_ROUNDS; ++round) {
        for (uint32_t cpu = 1; cpu <= XAT_APS; ++cpu) request_private(cpu,0);
        if (!xis_native_shootdown()) xat_abort();
    }
    if (ks_atomic_load_acquire(&xat_record.Irqs) < 2 * XAT_REQUEST_ROUNDS) xat_abort();
#ifdef KERNEL_TEST_AP_CONCURRENT
    if (ks_atomic_load_acquire(&xat_second_record.Irqs) < 2 * XAT_REQUEST_ROUNDS) xat_abort();
#endif
    for (uint32_t cpu = 1; cpu <= XAT_APS; ++cpu)
        if (!xad_release(cpu,0,1,root,1)) xat_abort();
    for (uint32_t cpu = 1; cpu <= XAT_APS; ++cpu) wait_private(cpu,XAD_QUIESCED,0);
#ifdef KERNEL_TEST_AP_CONCURRENT
    lot_final = kernel_scheduler_lot_on(0,KERNEL_LOT_QUERY,root,1,0);
    if (lot_final.status || lot_final.failed || lot_final.participants != 6 ||
        lot_final.entered != 6 || lot_final.completed != 6 || lot_final.quiesced !=
#ifdef KERNEL_TEST_AP_NO_QUIESCE
        2
#else
        6
#endif
        ||
        ks_atomic_load_acquire(&xat_arrived[0]) != 1 || ks_atomic_load_acquire(&xat_arrived[1]) != 1) xat_abort();
#endif
#ifdef KERNEL_TEST_AP_NO_QUIESCE
    if (kernel_scheduler_apply(KERNEL_SCHEDULER_COLLECT,0,xat_record.Generations[0]).status != KERNEL_SCHEDULER_BAD_STATE ||
        !kernel_physical_budget_matches(xat_pool,xat_record.Held)) xat_abort();
    for (uint32_t index = 0; index < XAT_TASKS; ++index)
        if (!x86_task_stack_validate(&xat_stacks[index])) xat_abort();
    if (!kernel_text("KERNEL_AP_BATCH_RECORD: ") || !kernel_number((uint64_t)(uintptr_t)&xat_record) ||
        !kernel_text("\nKERNEL_AP_BATCH_PTE: ") || !kernel_number((uint64_t)(uintptr_t)&x86_stack_entries(3)[320]) ||
        !kernel_text("\nKERNEL_AP_LOT_RECORD: ") || !kernel_number((uint64_t)(uintptr_t)&lot_final) ||
        !kernel_text("\n") || !kernel_log(1,"KERNEL_AP_LOT_INCOMPLETE")) xat_abort();
#endif
    xat_cleanup();
    for (uint32_t index = 1; index < xis_native.Count; ++index) request_private(index,1);
    if (xat_record.Completed != 2 || xat_record.Steps <
#ifdef KERNEL_TEST_AP_PREEMPTION
        8
#else
        34
#endif
         || xat_record.Collected != XAT_TASKS ||
        xad_value(1,root) != (1ULL << 3 | XAD_QUIESCED) ||
        !kernel_text("KERNEL_AP_BATCH_RECORD: ") || !kernel_number((uint64_t)(uintptr_t)&xat_record) ||
        !kernel_text("\nKERNEL_AP_BATCH_PTE: ") || !kernel_number((uint64_t)(uintptr_t)&x86_stack_entries(3)[320]) ||
        !kernel_text("\n") || !kernel_log(0,"KERNEL_AP_BATCH_READY")) xat_abort();
#ifdef KERNEL_TEST_AP_PREEMPTION
    if (!kernel_text("KERNEL_AP_PREEMPT_RECORD: ") ||
        !kernel_number((uint64_t)(uintptr_t)&xat_preempt) ||
        !kernel_text("\n") || !kernel_log(0,"KERNEL_AP_PREEMPT_READY")) xat_abort();
#endif
#ifdef KERNEL_TEST_AP_CONCURRENT
    if (xat_second_record.Completed != 2 || xat_second_record.Steps < 8 ||
        xad_value(2,root) != (1ULL << 3 | XAD_QUIESCED) ||
        !kernel_text("KERNEL_AP_SECOND_RECORD: ") || !kernel_number((uint64_t)(uintptr_t)&xat_second_record) ||
        !kernel_text("\nKERNEL_AP_SECOND_PREEMPT: ") || !kernel_number((uint64_t)(uintptr_t)&xat_preempt_second) ||
        !kernel_text("\nKERNEL_AP_LOT_RECORD: ") || !kernel_number((uint64_t)(uintptr_t)&lot_final) ||
        !kernel_text("\nKERNEL_AP_LOT_ARRIVED: ") || !kernel_number((uint64_t)(uintptr_t)xat_arrived) ||
        !kernel_text("\n") || !kernel_log(0,"KERNEL_AP_CONCURRENT_READY")) xat_abort();
#endif
    x86_kernel_stop(0x4b34495049504153ULL);
}
