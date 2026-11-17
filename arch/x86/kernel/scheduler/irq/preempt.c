#include <intrin.h>
#include "preempt.h"
#include "../integration/control.h"
#include "kernel/scheduler/preempt.h"
#include "kernel/sync/internal.h"
#include "kernel/diagnostics/log.h"
#include "arch/x86/kernel/interrupts/registry/registry.h"
#include "arch/x86/kernel/interrupts/apic/lvt.h"
#include "arch/x86/kernel/timer/lapic/runtime/runtime.h"
#include "arch/x86/mm/devices/lapic.h"

extern const unsigned char x86_scheduler_irq_compute_begin[], x86_scheduler_irq_compute_end[];

#define PREEMPT_LVT     (X86_LVT_TIMER_PERIODIC | X86_PREEMPT_VECTOR)
#define STOPPED_LVT     0x10041U
#define PREEMPT_DIVIDE  3U
#define PREEMPT_BANK    (X86_PREEMPT_VECTOR / 32U)
#define PREEMPT_BIT     (1U << (X86_PREEMPT_VECTOR % 32U))
#define DRAIN_ATTEMPTS  64U
#define DEVICE_CLASS    4ULL

_Static_assert((X86_PREEMPT_VECTOR >> 4) > DEVICE_CLASS, "preempt vector must stay above device class");

static struct {
    const X86_LAPIC_TIMER  *Timer;
    X86_IRQ_TOKEN           Token;
    KERNEL_PREEMPT_STATE    Policy;
    BOOT_U64                Ticks;
    BOOT_U64                Pauses, OutsideWindow;
    BOOT_U32                Open;
    BOOT_U32                Running;
    BOOT_U32                Lvt;
    BOOT_U32                Divide;
} preempt;

// ====== pending_private ======

// Intel SDM 078US Vol3A 11.8.4/Fig11-20: ISR 100h+16*bank, IRR 200h+16*bank, bit vector%32.
static int pending_private(BOOT_U32 *pending)
{
    BOOT_U32 service, request;
    if (!x86_lapic_read(preempt.Timer->Mapping,0x100 + PREEMPT_BANK * 16U,&service) ||
        !x86_lapic_read(preempt.Timer->Mapping,0x200 + PREEMPT_BANK * 16U,&request)) return 0;
    *pending = (service | request) & PREEMPT_BIT;
    return 1;
}

// ====== start_private ======

// Intel SDM 078US Vol3A 11.5.4: 3E0h divide 3 = /16, 320h bit17 periodic + vector, 380h initial count.
static int start_private(void)
{
    const X86_LAPIC_MAPPING *mapping = preempt.Timer->Mapping;
    return x86_timer_confirm_write(mapping,0x3e0,PREEMPT_DIVIDE) &&
        x86_timer_confirm_write(mapping,0x320,PREEMPT_LVT) &&
        x86_timer_write(mapping,0x380,preempt.Timer->Reload);
}

// ====== stop_private ======

// Intel SDM 078US Vol3A 11.8.3.1: delivery only if vector[7:4] > PPR[7:4].
// 11.8.6: CR8[3:0] = TPR[7:4]; class 4 holds 40h..4Fh in IRR, D1h (class Dh) is delivered.
static int stop_private(void)
{
    BOOT_U32 pending = 0;
    int drained = 0;
    if (!x86_timer_stop(preempt.Timer->Mapping) || __readcr8()) return 0;
    __writecr8(DEVICE_CLASS);
    for (BOOT_U32 attempt = 0; attempt <= DRAIN_ATTEMPTS; ++attempt) {
        if (!pending_private(&pending)) break;
        if (!pending) {
            drained = 1;
            break;
        }
        if (attempt == DRAIN_ATTEMPTS) break;
        _enable();
        __nop();
        __nop();
        _disable();
    }
    __writecr8(0);
    return drained && !__readcr8();
}

// ====== handle_private ======

static int handle_private(void)
{
    BOOT_U32 lvt;
    if (!preempt.Open || !x86_timer_read(preempt.Timer->Mapping,0x320,&lvt) ||
        lvt != (preempt.Running ? PREEMPT_LVT : STOPPED_LVT) ||
        !x86_lapic_eoi_vector(preempt.Timer->Mapping,X86_PREEMPT_VECTOR)) return 0;
    if (preempt.Ticks != UINT64_MAX) ++preempt.Ticks;
    return 1;
}

// ====== quiet_private ======

static int quiet_private(void)
{
    BOOT_U32 value, pending;
    const X86_LAPIC_MAPPING *mapping = preempt.Timer->Mapping;
    return x86_timer_read(mapping,0x320,&value) && value == STOPPED_LVT &&
        x86_timer_read(mapping,0x380,&value) && !value &&
        x86_timer_read(mapping,0x390,&value) && !value &&
        pending_private(&pending) && !pending;
}

// ====== dispatcher_private ======

static int dispatcher_private(void)
{
    const KERNEL_LOCK_CONTEXT *locks = kernel_lock_current_context();
    return !ks_irq_state() && locks && !locks->Depth && !x86_scheduler_task_valid();
}

// ====== x86_preempt_open ======

int x86_preempt_open(const X86_LAPIC_TIMER *timer)
{
    if (preempt.Open || preempt.Timer || !timer || !x86_timer_owner_valid(timer) || !timer->Reload ||
        timer->Error || !dispatcher_private()) return 0;
    preempt.Timer = timer;
    if (!x86_timer_read(timer->Mapping,0x320,&preempt.Lvt) ||
        !x86_timer_read(timer->Mapping,0x3e0,&preempt.Divide) || !x86_timer_stop(timer->Mapping) ||
        !x86_irq_registry_bind(x86_irq_registry(),X86_PREEMPT_VECTOR,handle_private,quiet_private,&preempt.Token)) {
        preempt.Timer = 0;
        return 0;
    }
    preempt.Open = 1;
    if (!start_private()) return 0;
    preempt.Running = 1;
    return 1;
}

// ====== x86_preempt_pause ======

int x86_preempt_pause(void)
{
    if (!preempt.Open) return 1;
    if (!preempt.Running || !dispatcher_private()) return 0;
    preempt.Running = 0;
    if (preempt.Pauses != UINT64_MAX) ++preempt.Pauses;
    return stop_private();
}

// ====== x86_preempt_resume ======

int x86_preempt_resume(void)
{
    if (!preempt.Open) return 1;
    if (preempt.Running || !dispatcher_private() || !start_private()) return 0;
    preempt.Running = 1;
    return 1;
}

// ====== x86_preempt_after ======

void x86_preempt_after(const X86_IRQ_FRAME *frame)
{
    KERNEL_PREEMPT_INPUT input = {0};
    const KERNEL_LOCK_CONTEXT *locks = kernel_lock_current_context();
    if (!preempt.Open || !preempt.Running || frame->Vector != X86_PREEMPT_VECTOR) return;
    if (x86_scheduler_task_valid())
        input.task = ((uint64_t)(x86_scheduler_index() + 1U) << 56) |
            (x86_scheduler_generation() & 0x00ffffffffffffffULL);
    input.interrupts_enabled = (frame->Flags & 0x200) != 0;
    input.lock_depth = locks ? locks->Depth : 1;
    input.line_open = (uint32_t)kernel_log_line_open();
    input.slice_over = input.task != 0;
    if (kernel_preempt_decide(&preempt.Policy,&input) != KERNEL_PREEMPT_SUSPEND
    )
        return;

    if ((frame->Rip < (BOOT_U64)x86_scheduler_irq_compute_begin ||
         frame->Rip >= (BOOT_U64)x86_scheduler_irq_compute_end) &&
        preempt.OutsideWindow != UINT64_MAX
    )
        ++preempt.OutsideWindow;


    x86_scheduler_tick_suspend();
}

// ====== x86_preempt_close ======

int x86_preempt_close(void)
{
    const KERNEL_PREEMPT_STATE *policy = &preempt.Policy;
    const X86_LAPIC_MAPPING *mapping;
    BOOT_U64 delivered;
    if (!preempt.Open || !dispatcher_private()) return 0;
    mapping = preempt.Timer->Mapping;
    preempt.Running = 0;
    if (!stop_private()) return 0;
    delivered = x86_irq_registry_deliveries(x86_irq_registry(),&preempt.Token);
    if (delivered != preempt.Ticks || !x86_irq_registry_unbind(x86_irq_registry(),&preempt.Token) ||
        !x86_timer_confirm_write(mapping,0x3e0,preempt.Divide) ||
        !x86_timer_confirm_write(mapping,0x320,preempt.Lvt)) return 0;
    preempt.Open = 0;
    preempt.Timer = 0;
    return kernel_text("KERNEL_PREEMPT_TICKS: ") && kernel_number(preempt.Ticks) &&
        kernel_text("\nKERNEL_PREEMPT_SUSPENDED: ") && kernel_number(policy->preempted) &&
        kernel_text("\nKERNEL_PREEMPT_OUTSIDE_WINDOW: ") && kernel_number(preempt.OutsideWindow) &&
        kernel_text("\nKERNEL_PREEMPT_IGNORED: ") && kernel_number(policy->ignored) &&
        kernel_text("\nKERNEL_PREEMPT_DEFERRED_LINE: ") && kernel_number(policy->deferred_line) &&
        kernel_text("\nKERNEL_PREEMPT_OVERRUNS: ") && kernel_number(policy->overruns) &&
        kernel_text("\nKERNEL_PREEMPT_PAUSES: ") && kernel_number(preempt.Pauses) &&
        kernel_text("\n") && kernel_log(0,"KERNEL_PREEMPT_CLOSED");
}
