#include <intrin.h>
#include "preempt.h"
#include "../integration/control.h"
#include "kernel/scheduler/preempt.h"
#include "kernel/sync/lock.h"
#include "kernel/sync/internal.h"
#include "kernel/diagnostics/log.h"
#include "arch/x86/kernel/interrupts/registry/registry.h"
#include "arch/x86/kernel/interrupts/apic/lvt.h"
#include "arch/x86/kernel/timer/lapic/runtime/runtime.h"
#include "arch/x86/mm/devices/lapic.h"
#include "../integration/local.h"
#ifdef KERNEL_AP_TASKS
#include "arch/x86/kernel/cpu/percpu/service/native/native.h"
#endif
#ifdef KERNEL_USER_PROCESS
int x86_process_in_syscall(void);
void x86_gui_clock_tick(void);
#endif

extern const unsigned char x86_scheduler_irq_compute_begin[], x86_scheduler_irq_compute_end[];

#define PREEMPT_LVT     X86_PREEMPT_VECTOR
#define STOPPED_LVT     0x10041U
#define PREEMPT_DIVIDE  3U
#define PREEMPT_BANK    (X86_PREEMPT_VECTOR / 32U)
#define PREEMPT_BIT     (1U << (X86_PREEMPT_VECTOR % 32U))
#define DRAIN_ATTEMPTS  64U
#define DEVICE_CLASS    4ULL

_Static_assert((X86_PREEMPT_VECTOR >> 4) > DEVICE_CLASS, "preempt vector must stay above device class");

typedef __declspec(align(64)) struct {
    const X86_LAPIC_TIMER  *Timer;
    X86_IRQ_TOKEN           Token;
    KERNEL_PREEMPT_STATE    Policy;
    BOOT_U64                Ticks;
    BOOT_U64                Pauses, OutsideWindow;
    BOOT_U64                Resumed;
    BOOT_U32                Open;
    BOOT_U32                Running;
    BOOT_U32                Lvt;
    BOOT_U32                Divide;
    X86_LAPIC_TIMER         LocalTimer;
    BOOT_U64                Reserved[3];
} X86_PREEMPT_CPU;

static X86_PREEMPT_CPU preempt_cpus[X86_PERCPU_CAPACITY];
_Static_assert(sizeof(X86_PREEMPT_CPU) == 320 &&
    offsetof(X86_PREEMPT_CPU, Policy) == 24 &&
    offsetof(X86_PREEMPT_CPU, Ticks) == 88 &&
    offsetof(X86_PREEMPT_CPU, Resumed) == 112 &&
    offsetof(X86_PREEMPT_CPU, Open) == 120, "Per-CPU preemption snapshot ABI");

// ====== cpu_private ======

static uint32_t cpu_private(void) {
    const X86_CPU_LOCAL *local = x86_percpu_current_local();
    return local ? local->Index : 0;
}

#define preempt preempt_cpus[cpu_private()]

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

// Intel SDM 253668-082US Vol. 3A 11.5.4/11.5.4.1: 320h bits18:17=00 one-shot,
// mask16=0, vector7:0=D1h, reserved bits zero; 3E0h R/W divide3=/16, 380h R/W initial count.
static int start_private(void)
{
    const X86_LAPIC_MAPPING *mapping = preempt.Timer->Mapping;
    return x86_timer_confirm_write(mapping,0x3e0,PREEMPT_DIVIDE) &&
        x86_timer_confirm_write(mapping,0x320,PREEMPT_LVT) &&
        x86_timer_confirm_write(mapping,0x380,preempt.Timer->Reload);
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

// ====== x86_preempt_rearm ======

// Intel SDM 253668-082US Vol. 3A 11.5.4/11.8.4: restart count only after draining old IRR/ISR.
int x86_preempt_rearm(void) {
    X86_PREEMPT_CPU *state = &preempt;
    if (!state->Open ||
        !state->Running
    )
        return 1;

    state->Running = 0;
    if (!stop_private() ||
        !start_private()
    )
        return 0;

    state->Running = 1;
    return 1;
}

// ====== handle_private ======

static int handle_private(void)
{
    BOOT_U32 lvt;
    if (!preempt.Open || !x86_timer_read(preempt.Timer->Mapping,0x320,&lvt) ||
        lvt != (preempt.Running ? PREEMPT_LVT : STOPPED_LVT) ||
        !x86_lapic_eoi_vector(preempt.Timer->Mapping,X86_PREEMPT_VECTOR)) return 0;
#ifdef KERNEL_USER_PROCESS
    if (cpu_private() == 0)
        x86_gui_clock_tick();
#endif
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
#ifdef KERNEL_USER_PROCESS
    if (x86_process_in_syscall()) {
        if (!x86_preempt_rearm())
            kernel_panic("PREEMPT_REARM_FAILED");
        return;
    }
#endif
    if (x86_scheduler_task_valid())
        input.task = ((uint64_t)(x86_scheduler_index() + 1U) << 56) |
            (x86_scheduler_generation() & 0x00ffffffffffffffULL);
    input.interrupts_enabled = (frame->Flags & 0x200) != 0;
    input.lock_depth = locks ? locks->Depth : 1;
    input.line_open = cpu_private() ? 0 : (uint32_t)kernel_log_line_open();
    input.slice_over = input.task != 0;
    if (kernel_preempt_decide(&preempt.Policy,&input) != KERNEL_PREEMPT_SUSPEND
    ) {
        if (!x86_preempt_rearm()
        )
            kernel_panic("PREEMPT_REARM_FAILED");

        return;
    }

    if ((frame->Rip < (BOOT_U64)x86_scheduler_irq_compute_begin ||
         frame->Rip >= (BOOT_U64)x86_scheduler_irq_compute_end) &&
        preempt.OutsideWindow != UINT64_MAX
    )
        ++preempt.OutsideWindow;


    BOOT_U64 *resumed = &preempt.Resumed;
    x86_scheduler_tick_suspend();
    BOOT_U64 completed = ks_atomic_load_relaxed(resumed);
    while (completed != UINT64_MAX) {
        if (ks_atomic_compare(resumed,completed,completed + 1))
            break;
        completed = ks_atomic_load_relaxed(resumed);
    }

    if (!x86_preempt_rearm()
    )
        kernel_panic("PREEMPT_REARM_FAILED");
}

// ====== kernel_preempt_log_boundary ======

void kernel_preempt_log_boundary(void) {
    if (cpu_private())
        return;
    if (!preempt.Open || !preempt.Running) return;
    uint64_t saved_if = kernel_preempt_section_enter();
    const KERNEL_LOCK_CONTEXT *locks = kernel_lock_current_context();
    uint64_t task = 0;
    if (x86_scheduler_task_valid()) {
        task = ((uint64_t)(x86_scheduler_index() + 1U) << 56) |
            (x86_scheduler_generation() & 0x00ffffffffffffffULL);
    }
    if (kernel_preempt_line_complete(&preempt.Policy,task,(saved_if & 0x200) != 0,
        locks ? locks->Depth : 1) == KERNEL_PREEMPT_SUSPEND
    ) {
        x86_scheduler_tick_suspend();
        if (!x86_preempt_rearm()
        )
            kernel_panic("PREEMPT_LINE_REARM_FAILED");
    }

    kernel_preempt_section_leave(saved_if);
}

// ====== x86_preempt_close ======

int x86_preempt_close(void)
{
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
    return x86_preempt_report() && kernel_log(0,"KERNEL_PREEMPT_CLOSED");
}

// ====== x86_preempt_ticks ======

BOOT_U64 x86_preempt_ticks(void)
{
    return preempt.Ticks;
}

#ifdef KERNEL_AP_TASKS
// ====== x86_preempt_cpu_progress ======

BOOT_U64 x86_preempt_cpu_progress(BOOT_U32 cpu) {
    return cpu < X86_PERCPU_CAPACITY ? ks_atomic_load_acquire(&preempt_cpus[cpu].Resumed) : UINT64_MAX;
}

// ====== x86_preempt_ap_open ======

// Intel SDM 253668-082US Vol. 3A 11.5.4: LVT320h mask16, one-shot18:17=00,
// Initial380h R/W, Current390h R/O, Divide3e0h R/W encoding3=/16.
int x86_preempt_ap_open(void) {
    const X86_LAPIC_TIMER *reference = x86_timer_owner();
    uint32_t initial, current;
    if (!cpu_private() ||
        preempt.Open ||
        !reference ||
        !reference->Reload ||
        !dispatcher_private()
    )
        return 0;
    X86_LAPIC_TIMER *timer = &preempt.LocalTimer;
    timer->Mapping = reference->Mapping;
    timer->Clock = reference->Clock;
    return x86_timer_stop(timer->Mapping) &&
        x86_timer_confirm_write(timer->Mapping,0x3e0,PREEMPT_DIVIDE) &&
        x86_timer_measure_local(timer) && x86_preempt_open(timer) &&
        x86_timer_read(timer->Mapping,0x380,&initial) && initial == timer->Reload &&
        x86_timer_read(timer->Mapping,0x390,&current) && current != 0;
}

// ====== x86_preempt_task_frame_valid ======

int x86_preempt_task_frame_valid(const X86_IRQ_FRAME *frame) {
    X86_SCHEDULER_LOCAL *local = x86_scheduler_local_current();
    if (!local ||
        !cpu_private() ||
        local->current >= x86_scheduler_capacity() ||
        !x86_scheduler_task_valid()
    )
        return 0;
    uint64_t base = x86_task_stack_base(local->current);
    return x86_irq_saved_area((uint64_t)(uintptr_t)frame,base,base + X86_TASK_STACK_BYTES) == 1 &&
        xis_task_return_valid((uint64_t)(uintptr_t)frame,frame->Rsp,frame->Cs,frame->Flags,frame->Ss) == 1;
}
#endif

// ====== x86_preempt_report ======

int x86_preempt_report(void)
{
    const KERNEL_PREEMPT_STATE *policy = &preempt.Policy;
    return kernel_text("KERNEL_PREEMPT_CPUS: ") && kernel_number((BOOT_U64)preempt_cpus) &&
        kernel_text("\nKERNEL_PREEMPT_CPU_BYTES: ") && kernel_number(sizeof(X86_PREEMPT_CPU)) &&
        kernel_text("\nKERNEL_PREEMPT_TICKS: ") && kernel_number(preempt.Ticks) &&
        kernel_text("\nKERNEL_PREEMPT_SUSPENDED: ") && kernel_number(policy->preempted) &&
        kernel_text("\nKERNEL_PREEMPT_OUTSIDE_WINDOW: ") && kernel_number(preempt.OutsideWindow) &&
        kernel_text("\nKERNEL_PREEMPT_IGNORED: ") && kernel_number(policy->ignored) &&
        kernel_text("\nKERNEL_PREEMPT_DEFERRED_LINE: ") && kernel_number(policy->deferred_line) &&
        kernel_text("\nKERNEL_PREEMPT_OVERRUNS: ") && kernel_number(policy->overruns) &&
        kernel_text("\nKERNEL_PREEMPT_PAUSES: ") && kernel_number(preempt.Pauses) &&
        kernel_text("\n");
}
