#include "arch/x86/kernel/scheduler/admission/admission.h"
#include "arch/x86/kernel/cpu/percpu/service/native/native.h"
#include "arch/x86/kernel/stack/main.h"
#include "kernel/diagnostics/log.h"

// ====== request_private ======

static void request_private(uint32_t index)
{
    uint64_t token = xis_publish(index,0);
    XIS_WAIT wait = {xis_native_now,NULL,xp_panic_native.Source.Mask,KERNEL_PM_TIMER_HZ,1000000,0};
    if (!token || token == XIS_INVALID || !xis_native_send(index) ||
        xis_wait(index,token,&wait) != XIS_WAIT_ACK || !xis_native_wait(index,1,token))
        xis_native_abort(0x4b3441444d50494eULL);
}

// ====== fail_private ======

__declspec(noreturn) static void fail_private(uint64_t root)
{
    (void)xad_fail(1,0,1,root);
    xis_native_abort(0x4b3441444d54494dULL);
}

// ====== xad_admission_probe ======

__declspec(noreturn) void xad_admission_probe(void)
{
    uint64_t root = x86_kernel_root();
    if (xis_native_index() || ks_irq_state() || !xad_configure(xis_native.Count,root,0)) fail_private(root);
    for (uint32_t index = 1; index < xis_native.Count; ++index) request_private(index);
    if (!xad_offer(1,0,1,root)) fail_private(root);
    request_private(1);
    uint32_t start, now, admitted = 0;
    if (!xis_native_now(NULL,&start)) fail_private(root);
    for (uint32_t poll = 0; poll < 1000000; ++poll) {
        uint64_t value = xad_value(1,root);
        if (value == (1ULL << 3 | XAD_ADMITTED)) { admitted = 1; break; }
        if (value != (1ULL << 3 | XAD_OFFERED) && value != (1ULL << 3 | XAD_CLAIMED)) break;
        if (!xis_native_now(NULL,&now) || ((now - start) & xp_panic_native.Source.Mask) >= KERNEL_PM_TIMER_HZ) break;
        ks_pause();
    }
    if (!admitted || xad_native_record.Self != (uint64_t)(uintptr_t)&xad_native_record ||
        xad_native_record.Token != 1 || xad_native_record.Cpu != 1 ||
        xad_native_record.Root != root || xad_native_record.Group != root || xad_native_record.Irq)
        fail_private(root);
    for (uint32_t index = 1; index < xis_native.Count; ++index) request_private(index);
    if (!xis_native_shootdown()) fail_private(root);
    for (uint32_t index = 1; index < xis_native.Count; ++index) {
        if (!xis_native_wait(index,1,xis_value(index,XIS_ACK)) ||
            xad_value(index,root) != (index == 1 ? (1ULL << 3 | XAD_ADMITTED) : 0)) fail_private(root);
    }
    if (!kernel_text("KERNEL_AP_DISPATCH_RECORD: ") ||
        !kernel_number((uint64_t)(uintptr_t)&xad_native_record) || !kernel_text("\n") ||
        !kernel_log(0,"KERNEL_AP_DISPATCH_ADMITTED")) fail_private(root);
    x86_kernel_stop(0x4b34495049504153ULL);
}
