#include "admission.h"
#include "../integration/local.h"
#include "arch/x86/kernel/cpu/percpu/service/native/native.h"
#include "arch/x86/kernel/cpu/percpu/startup/private/frame.h"
#include "arch/x86/kernel/stack/main.h"

volatile XAD_NATIVE_RECORD xad_native_record;

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
    if (!token) return;
    if (token == XAD_INVALID || index != 1 || ks_irq_state() || !x86_irq_cpu_state())
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
        !x86_irq_verify(&xis_native.Idt,&xis_native.Idtr,&xp_panic_native.Idt,
            xis_irq_targets,(uint64_t)xis_irq_begin,(uint64_t)xis_irq_end)) fail_private(index,token,root);
    X86_SCHEDULER_LOCAL *local = x86_scheduler_local_prepare();
    if (!local || local->phase || local->current != UINT32_MAX || local->generation || local->event ||
        local->stats.steps || local->stats.failure ||
        local->owner != (uint64_t)(uintptr_t)xp_panic_native.Contexts[index]) fail_private(index,token,root);
    xad_native_record.Self = (uint64_t)(uintptr_t)&xad_native_record;
    xad_native_record.Group = root; xad_native_record.Token = token; xad_native_record.Cpu = index;
    xad_native_record.Rsp = rsp; xad_native_record.Local = (uint64_t)(uintptr_t)local;
    xad_native_record.Root = root; xad_native_record.Irq = ks_irq_state();
    if (!xad_admit(index,index,token,root)) fail_private(index,token,root);
}
