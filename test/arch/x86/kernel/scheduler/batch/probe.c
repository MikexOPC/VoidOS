#include "arch/x86/kernel/scheduler/batch/batch.h"
#include "arch/x86/kernel/cpu/percpu/service/native/native.h"
#include "kernel/diagnostics/log.h"

// ====== request_private ======

static void request_private(uint32_t index, uint32_t idle)
{
    uint64_t token = xis_publish(index,0);
    XIS_WAIT wait = {xis_native_now,NULL,xp_panic_native.Source.Mask,KERNEL_PM_TIMER_HZ,1000000,0};
    if (!token || token == XIS_INVALID || !xis_native_send(index) ||
        xis_wait(index,token,&wait) != XIS_WAIT_ACK || (idle && !xis_native_wait(index,1,token))) xat_abort();
}

// ====== wait_private ======

static void wait_private(uint64_t phase, uint32_t progress)
{
    uint32_t start, now;
    if (!xis_native_now(NULL,&start)) xat_abort();
    for (uint32_t poll = 0; poll < 1000000; ++poll) {
        uint64_t value = xad_value(1,x86_kernel_root());
        if (value == (1ULL << 3 | phase) && (!progress ||
            (ks_atomic_load_acquire(&xat_record.Progress[0]) && ks_atomic_load_acquire(&xat_record.Progress[1])))) return;
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
    if (!xad_offer(1,0,1,root)) xat_abort();
    request_private(1,1); wait_private(XAD_ADMITTED,0);
    xat_prepare();
    if (!xad_transition(1,0,1,root,0)) xat_abort();
    request_private(1,0); wait_private(XAD_RUNNING,1);
    request_private(1,0);
    if (!xis_native_shootdown() || ks_atomic_load_acquire(&xat_record.Irqs) < 2 ||
        !xad_release(1,0,1,root,1)) xat_abort();
    wait_private(XAD_QUIESCED,0);
    xat_cleanup();
    for (uint32_t index = 1; index < xis_native.Count; ++index) request_private(index,1);
    if (xat_record.Completed != 2 || xat_record.Steps < 34 || xat_record.Collected != 2 ||
        xad_value(1,root) != (1ULL << 3 | XAD_QUIESCED) ||
        !kernel_text("KERNEL_AP_BATCH_RECORD: ") || !kernel_number((uint64_t)(uintptr_t)&xat_record) ||
        !kernel_text("\nKERNEL_AP_BATCH_PTE: ") || !kernel_number((uint64_t)(uintptr_t)&x86_stack_entries(3)[320]) ||
        !kernel_text("\n") || !kernel_log(0,"KERNEL_AP_BATCH_READY")) xat_abort();
    x86_kernel_stop(0x4b34495049504153ULL);
}
