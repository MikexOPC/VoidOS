#include "admission.h"
#include "../integration/local.h"
#include "arch/x86/kernel/cpu/percpu/service/native/native.h"
#include "arch/x86/kernel/cpu/percpu/startup/private/frame.h"
#include "arch/x86/kernel/stack/main.h"
#if KERNEL_TEST_AP_SERVICE == 9
#include "../batch/batch.h"
#elif defined(KERNEL_AP_TASKS)
#include "../remote/remote.h"
#endif

volatile XAD_NATIVE_RECORD xad_native_record;
#ifdef KERNEL_TEST_AP_CONCURRENT
static XAD_NATIVE_RECORD peer_records[XAT_APS - 1];
#endif

// ====== fail_private ======

__declspec(noreturn) static void fail_private(uint32_t index, uint64_t token, uint64_t root)
{
    (void)xad_fail(index,index,token,root);
    xis_native_abort(0x4b3441444d464149ULL);
}

// ====== xad_native_poll ======

void xad_native_poll(void)
{
    uint32_t index = xis_native_index();
    uint64_t root = x86_kernel_root();
    uint64_t token = xad_claim(index,index,root);
    if (!token) {
#if KERNEL_TEST_AP_SERVICE == 9
        if (index >= 1 && index <= XAT_APS && xad_transition(index,index,1,root,1)) {
            xat_run();
            if (!xad_transition(index,index,1,root,2)) fail_private(index,1,root);
        }
#elif defined(KERNEL_AP_TASKS)
        uint64_t value = xad_value(index,root);
        if (value != XAD_INVALID && (value & 7) == XAD_START &&
            xad_transition(index,index,value >> 3,root,1)) {
            x86_remote_ap_run();
            if (!xad_transition(index,index,value >> 3,root,2)) fail_private(index,value >> 3,root);
        }
#endif
        return;
    }
    if (token == XAD_INVALID ||
#ifdef KERNEL_TEST_AP_CONCURRENT
        index < 1 || index > XAT_APS ||
#else
        index != 1 ||
#endif
        ks_irq_state() || !x86_irq_cpu_state())
        fail_private(index,token,root);
    uint64_t rsp = x86_kernel_stack_pointer();
    if (rsp < X86_AP_STACK_FLOOR(index) || rsp >= X86_AP_FRAME(index) - XIS_IDLE_FRAME_BYTES)
        fail_private(index,token,root);
    X86_KERNEL_IDTR actual;
    x86_exception_read_idt(&actual);
    for (uint32_t byte = 0; byte < sizeof(actual); ++byte)
        if (actual.Bytes[byte] != xis_native.Idtr.Bytes[byte]) fail_private(index,token,root);
    const XS_PRIVATE_FRAME *frame = (const XS_PRIVATE_FRAME *)(uintptr_t)X86_AP_FRAME(index);
    if (frame->Phase != 6 || frame->Error || root != xp_panic_native.Root ||
        !x86_irq_shared_valid()) fail_private(index,token,root);
    X86_SCHEDULER_LOCAL *local = x86_scheduler_local_prepare();
    if (!local || local->phase || local->current != UINT32_MAX || local->generation || local->event ||
        local->stats.steps || local->stats.failure ||
        local->owner != (uint64_t)(uintptr_t)xp_panic_native.Contexts[index]) fail_private(index,token,root);
#ifdef KERNEL_TEST_AP_CONCURRENT
    volatile XAD_NATIVE_RECORD *record = index == 1 ? &xad_native_record : &peer_records[index - 2];
#else
    volatile XAD_NATIVE_RECORD *record = &xad_native_record;
#endif
    record->Self = (uint64_t)(uintptr_t)record;
    record->Group = root; record->Token = token; record->Cpu = index;
    record->Rsp = rsp; record->Local = (uint64_t)(uintptr_t)local;
    record->Root = root; record->Irq = ks_irq_state();
    if (!xad_admit(index,index,token,root)) fail_private(index,token,root);
}
