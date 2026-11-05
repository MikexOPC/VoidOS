#include "batch.h"
#include "kernel/sync/internal.h"
#include "../irq/bridge.h"
#include "arch/x86/kernel/cpu/percpu/service/native/native.h"

extern const unsigned char x86_scheduler_irq_compute_begin[], x86_scheduler_irq_compute_end[];
XAT_PREEMPT xat_preempt;
static volatile uint32_t completion[2][2];
_Static_assert(sizeof(XAT_PREEMPT) == 128,"AP preemption evidence ABI");

// ====== xat_preempt_quiet ======

// Intel SDM 078US §11.5.4: LVT320h, Initial380h, Current390h, Divide3e0h.
// LVT mask16 and periodic17; divide encoding3 is /16. §11.8.5 EOI=b0h.
int xat_preempt_quiet(void)
{
    const uint32_t bit = 1U << (0xd1 % 32), offset = (0xd1 / 32) * 16;
    return xat_preempt.Stopped == 1 && xat_preempt.Total == 6 &&
        xat_preempt.Ticks[0] == 3 && xat_preempt.Ticks[1] == 3 &&
        xat_preempt.Resumed[0] == 3 && xat_preempt.Resumed[1] == 3 &&
        !xis_mmio_read(0x380) && !xis_mmio_read(0x390) &&
        xis_mmio_read(0x320) == 0x300d1 && xis_mmio_read(0x3e0) == 3 &&
        !(xis_mmio_read(0x100 + offset) & bit) && !(xis_mmio_read(0x200 + offset) & bit);
}

// ====== xat_preempt_work ======

void xat_preempt_work(uint32_t index)
{
    if (index >= 2 || !xat_task_valid() || xat_preempt.Ticks[index] ||
        xat_preempt.Resumed[index] || !xat_preempt.Reload || xat_preempt.Reload > UINT32_MAX) xat_abort();
    if (!index) {
        if (xis_mmio_read(0x380) || xis_mmio_read(0x390) || xis_mmio_read(0x320) != 0x10040) xat_abort();
        xis_mmio_write(0x3e0,3);
        xis_mmio_write(0x320,0x200d1);
        xis_mmio_write(0x380,(uint32_t)xat_preempt.Reload);
        if (xis_mmio_read(0x3e0) != 3 || xis_mmio_read(0x320) != 0x200d1 ||
            xis_mmio_read(0x380) != xat_preempt.Reload) xat_abort();
    }
    if (x86_scheduler_irq_poll(completion[index],xp_panic_native.Source.Port,
        xp_panic_native.Source.Mask,KERNEL_PM_TIMER_HZ / 2) != 1 ||
        xat_preempt.Resumed[index] != 3) xat_abort();
}

// ====== xat_preempt_interrupt ======

void xat_preempt_interrupt(const X86_IRQ_FRAME *frame)
{
    X86_SCHEDULER_LOCAL *local = x86_scheduler_local_current();
    uint32_t index = local ? local->current : UINT32_MAX;
    const uint32_t bit = 1U << (0xd1 % 32), isr = 0x100 + (0xd1 / 32) * 16;
    if (!xat_ap_actor() || !xat_task_valid() || index >= 2 || frame->Vector != 0xd1 ||
        xis_mmio_read(isr) != bit || ks_irq_state()) xat_abort();
    if (xat_preempt.Stopped) {
        if (++xat_preempt.Late > 8) xat_abort();
        xis_mmio_write(0xb0,0);
        if (xis_mmio_read(isr)) xat_abort();
        return;
    }
    if (index != xat_preempt.Total % 2 || xat_preempt.Total >= 6 ||
        xat_preempt.Ticks[index] != xat_preempt.Resumed[index] ||
        frame->Rip < (uint64_t)(uintptr_t)x86_scheduler_irq_compute_begin ||
        frame->Rip >= (uint64_t)(uintptr_t)x86_scheduler_irq_compute_end) xat_abort();
    if (xat_preempt.Total == 5) {
        xis_mmio_write(0x320,0x300d1);
        xis_mmio_write(0x380,0);
        if (xis_mmio_read(0x320) != 0x300d1 || xis_mmio_read(0x380) || xis_mmio_read(0x390)) xat_abort();
        xat_preempt.Stopped = 1;
    }
    xis_mmio_write(0xb0,0);
    if (xis_mmio_read(isr)) xat_abort();
    ++xat_preempt.Total;
    if (++xat_preempt.Ticks[index] == 3) completion[index][0] = 1;
    xat_preempt.Frame[index] = (uint64_t)(uintptr_t)frame;
    xat_preempt.Rsp[index] = frame->Rsp;
    xat_preempt.Rip[index] = frame->Rip;
}

// ====== xat_preempt_after ======

// Intel SDM 078US §6.14: suspend only after EOI, before FXRSTOR/IRETQ.
void xat_preempt_after(const X86_IRQ_FRAME *frame)
{
    if (frame->Vector != 0xd1) return;
    X86_SCHEDULER_LOCAL *local = x86_scheduler_local_current();
    const KERNEL_LOCK_CONTEXT *locks = kernel_lock_current_context();
    uint32_t index = local ? local->current : UINT32_MAX;
    if (!local || index >= 2 || !xat_task_valid() || !locks || locks->Depth ||
        ks_irq_state() || local->event || !(frame->Flags & 512)) xat_abort();
    if (xat_preempt.Ticks[index] == xat_preempt.Resumed[index]) return;
    if (xat_preempt.Ticks[index] != xat_preempt.Resumed[index] + 1 ||
        xat_preempt.Frame[index] != (uint64_t)(uintptr_t)frame ||
        !x86_scheduler_context_resume_valid(&local->dispatcher,X86_AP_STACK_BASE(1),65536,
            X86_KERNEL_IMAGE_BASE,X86_KERNEL_IMAGE_LIMIT,xat_mask)) xat_abort();
    uint64_t generation = local->generation;
    local->event = X86_TASK_PREEMPT;
    x86_scheduler_context_switch(&xat_contexts[index],&local->dispatcher);
    local = x86_scheduler_local_current();
    if (!local || !xat_task_valid() || local->current != index || local->generation != generation ||
        local->event || xat_preempt.Resumed[index] >= 3) xat_abort();
    ++xat_preempt.Resumed[index];
}
