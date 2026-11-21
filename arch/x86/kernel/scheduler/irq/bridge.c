#include "bridge.h"
#include "kernel/scheduler/payload.h"
#include "../idle/idle.h"
#include "../integration/control.h"
#include "arch/x86/kernel/timer/lapic/runtime/runtime.h"
#include "arch/x86/kernel/interrupts/return/table.h"
#include "kernel/diagnostics/log.h"
static X86_LAPIC_TIMER *timer_source;
static uint32_t posted, finished;
#ifdef KERNEL_TEST_SCHEDULER_PERIODIC
#include "kernel/sync/internal.h"
static const X86_LAPIC_TIMER *periodic_source;
static X86_IRQ_TOKEN periodic_token;
static volatile uint32_t periodic_completion[2][2];
static uint32_t periodic_ticks[2], periodic_resumed[2], periodic_total;
#endif
static uint64_t binding_generation;
static uint64_t irq_rip, irq_rsp, irq_flags, irq_generation;
extern const unsigned char x86_scheduler_irq_compute_begin[], x86_scheduler_irq_compute_end[];
#ifdef KERNEL_PS2_INPUT
#include "arch/x86/kernel/drivers/input/ps2/runtime/session.h"
#endif

// ====== x86_scheduler_irq_record ======

int x86_scheduler_irq_record(uint64_t vector, uint64_t rip, uint64_t rsp, uint64_t flags)
{
#ifdef KERNEL_AP_TASKS
    if (x86_scheduler_task_valid() && !x86_scheduler_driver_task_valid()) {
        uint64_t base = x86_task_stack_base(x86_scheduler_index());
        return (flags & 0x600) == 0x200 &&
            rsp >= base + 4096 && rsp < base + X86_TASK_STACK_BYTES &&
            x86_scheduler_code_valid(rip);
    }
#endif
#ifdef KERNEL_PS2_INPUT
    if (x86_ps2_session()->Active) return x86_ps2_irq_record(vector,rip,rsp,flags);
#endif
#ifdef KERNEL_TEST_SCHEDULER_PERIODIC
    if (periodic_source) {
        uint32_t index = x86_scheduler_index();
        return vector == X86_TIMER_VECTOR && index < 2 && x86_scheduler_task_valid() &&
            (flags & 0x600) == 0x200 &&
            rip >= (uint64_t)x86_scheduler_irq_compute_begin && rip < (uint64_t)x86_scheduler_irq_compute_end &&
            rsp >= x86_task_stack_base(index) + 4096 && rsp < x86_task_stack_base(index) + 65536;
    }
#endif
    if (!timer_source || vector != X86_TIMER_VECTOR || irq_rip || x86_scheduler_index() != 1 ||
        !x86_scheduler_task_valid() || !(flags & 512) || (flags & 1024) ||
        rip < (uint64_t)x86_scheduler_irq_compute_begin || rip >= (uint64_t)x86_scheduler_irq_compute_end ||
        rsp < x86_task_stack_base(1) + 4096 || rsp >= x86_task_stack_base(1) + 65536 ||
        !x86_scheduler_code_valid(rip)) return 0;
    irq_rip = rip; irq_rsp = rsp; irq_flags = flags; irq_generation = x86_scheduler_generation();
    return irq_generation != 0;
}

// ====== x86_scheduler_timer_notify ======

int x86_scheduler_timer_notify(const X86_LAPIC_TIMER *source)
{
#ifdef KERNEL_PS2_INPUT
    if (!timer_source) return 0;
#else
    if (!timer_source) return x86_scheduler_idle_notify(source);
#endif
    if (source != timer_source || posted || x86_scheduler_index() != 1 ||
        !x86_scheduler_task_valid() || source->Count != 1 || source->Error ||
        source->Phase != X86_TIMER_PENDING ||
        kernel_scheduler_payload_push(2,(uint64_t)(uintptr_t)source,binding_generation,source->Count & 255) != 1) return 0;
    posted = 1;
    return 1;
}

#ifdef KERNEL_TEST_SCHEDULER_PERIODIC
// ====== x86_scheduler_irq_after ======

void x86_scheduler_irq_after(const X86_IRQ_FRAME *frame) {
    if (periodic_source && frame->Vector == X86_TIMER_VECTOR) {
        uint32_t index = x86_scheduler_index();
        const KERNEL_LOCK_CONTEXT *locks = kernel_lock_current_context();

        if (index >= 2 || !locks || locks->Depth || ks_irq_state() ||
            !(frame->Flags & 512) ||
            periodic_ticks[index] != periodic_resumed[index] + 1
        )
            kernel_scheduler_fail_stop();

        x86_scheduler_irq_suspend();
        ++periodic_resumed[index];
    }
}
#endif

#ifndef KERNEL_PS2_INPUT
// ====== x86_scheduler_irq_task ======

void x86_scheduler_irq_task(uint32_t index)
{
    if (!timer_source || !x86_scheduler_task_valid()) kernel_scheduler_fail_stop();
    if (index == 0) {
        KERNEL_EVENT_REPLY result = x86_scheduler_event_request(4, 2, 0);
        if (result.outcome != KERNEL_EVENT_WAITING || result.epoch || !finished) kernel_scheduler_fail_stop();
        if (!posted || timer_source->Count != 1 || timer_source->Error) kernel_scheduler_fail_stop();
        if (kernel_scheduler_payload_pop(2,(uint64_t)(uintptr_t)timer_source,binding_generation,
                index,x86_scheduler_generation()) != (KERNEL_PAYLOAD_DATA | 1) ||
            kernel_scheduler_payload_pop(2,(uint64_t)(uintptr_t)timer_source,binding_generation,
                index,x86_scheduler_generation()) != KERNEL_PAYLOAD_EMPTY) kernel_scheduler_fail_stop();
    } else if (index == 1) {
        int received;
        // Trusted owner/port/clock from the existing calibrated driver, no new MMIO.
        if (!x86_timer_owner_valid(timer_source) || !x86_irq_ready() || timer_source->Count ||
            x86_kernel_stack_pointer() < x86_task_stack_base(1) + 8192 ||
            timer_source->Error || !timer_source->Clock.Port || timer_source->Clock.Port > 65535 ||
            (timer_source->Clock.Mask != 0xFFFFFF && timer_source->Clock.Mask != UINT32_MAX))
            kernel_scheduler_fail_stop();
        if (!x86_timer_arm(timer_source)) {
            (void)kernel_log(1, "KERNEL_SCHEDULER_IRQ_ARM_FAILED");
            kernel_scheduler_fail_stop();
        }
#if defined(KERNEL_TEST_SCHEDULER_FAULT) && KERNEL_TEST_SCHEDULER_FAULT == 7
        // Isolated missing-source injection through the driver's verified stop API.
        // Preserve logical owner/subscription and both task leases on terminal failure.
        if (!x86_timer_stop(timer_source->Mapping)) kernel_scheduler_fail_stop();
#endif
        received = x86_scheduler_irq_poll(&timer_source->Count, timer_source->Clock.Port,
            timer_source->Clock.Mask, X86_PM_CLOCK_HZ / 10);
#if defined(KERNEL_TEST_SCHEDULER_FAULT) && KERNEL_TEST_SCHEDULER_FAULT == 7
        if (received != 2 || posted || timer_source->Count || timer_source->Error ||
            !x86_scheduler_irq_timeout_valid() || !kernel_log(1,"KERNEL_SCHEDULER_IRQ_TIMEOUT_OK"))
            kernel_scheduler_fail_stop();
        kernel_scheduler_fail_stop();
#else
        if (received != 1 || !posted || timer_source->Error) {
            if (received == 2 && !timer_source->Count && !timer_source->Error)
                (void)kernel_log(1, "KERNEL_SCHEDULER_IRQ_DELIVERY_TIMEOUT");
            else
                (void)kernel_log(1, "KERNEL_SCHEDULER_IRQ_COMPLETION_FAILED");
            kernel_scheduler_fail_stop();
        }
        finished = 1;
#endif
    } else kernel_scheduler_fail_stop();
}

// ====== x86_scheduler_irq_verified ======

int x86_scheduler_irq_verified(void)
{
    return timer_source && irq_rip && irq_rsp && irq_generation && (irq_flags & 512) &&
        posted == 1 && finished == 1 && timer_source->Count == 1 &&
        !timer_source->Error && timer_source->Phase == X86_TIMER_PENDING;
}

// ====== x86_scheduler_timer_tasks ======

int x86_scheduler_timer_tasks(X86_LAPIC_TIMER *source)
{
    if (timer_source || posted || finished || !source || !x86_timer_owner_valid(source) ||
        !x86_irq_ready() || source->Count || source->Consumed || source->Error ||
        source->Phase != X86_TIMER_READY || source->Active) return 0;
    binding_generation = kernel_scheduler_irq_bind(2,(uint64_t)(uintptr_t)source);
    if (!binding_generation) return 0;
    if (!kernel_scheduler_payload_open(2,(uint64_t)(uintptr_t)source,binding_generation,0)) return 0;
    timer_source = source;
    if (!x86_scheduler_run_irq() || !x86_scheduler_irq_verified()) return 0;
    // Source has been stopped and acknowledged by its existing IRQ callback.
    // Active denotes the logical unconsumed timer event, not hardware arming.
    if (!x86_timer_owner_valid(source) || !x86_lapic_idle(source->Mapping) ||
        !kernel_scheduler_payload_close(2,(uint64_t)(uintptr_t)source,binding_generation) ||
        !kernel_scheduler_irq_unbind(2,(uint64_t)(uintptr_t)source,binding_generation)) return 0;
    timer_source = 0;
    if (kernel_scheduler_irq_post_bound(2,(uint64_t)(uintptr_t)source,binding_generation)) return 0;
    return kernel_text("SCHEDULER_IRQ_RIP: ") && kernel_number(irq_rip) &&
        kernel_text("\nSCHEDULER_IRQ_BEGIN: ") && kernel_number((uint64_t)x86_scheduler_irq_compute_begin) &&
        kernel_text("\nSCHEDULER_IRQ_END: ") && kernel_number((uint64_t)x86_scheduler_irq_compute_end) &&
        kernel_text("\nSCHEDULER_IRQ_RSP: ") && kernel_number(irq_rsp) &&
        kernel_text("\nSCHEDULER_IRQ_FLAGS: ") && kernel_number(irq_flags) &&
        kernel_text("\nSCHEDULER_IRQ_GENERATION: ") && kernel_number(irq_generation) &&
        kernel_text("\nSCHEDULER_IRQ_TASK: ") && kernel_number(1) && kernel_text("\n") &&
        kernel_log(0,"KERNEL_SCHEDULER_TASK_IRQ_OK");
}

#endif

#ifdef KERNEL_TEST_SCHEDULER_PERIODIC
// ====== periodic_quiet ======

static int periodic_quiet(void)
{
    BOOT_U32 value;
    const X86_LAPIC_MAPPING *mapping = periodic_source->Mapping;
    return x86_timer_read(mapping,0x320,&value) && value == (X86_LVT_MASK | X86_TIMER_VECTOR) &&
        x86_timer_read(mapping,0x380,&value) && !value &&
        x86_timer_read(mapping,0x390,&value) && !value && x86_lapic_idle(mapping);
}

// ====== periodic_irq ======

static int periodic_irq(void)
{
    uint32_t index = x86_scheduler_index();
    if (!periodic_source || index >= 2 || index != periodic_total % 2 ||
        periodic_ticks[index] >= 3 || periodic_total >= 6) return 0;
    if (periodic_total == 5 && !x86_timer_stop(periodic_source->Mapping)) return 0;
    if (!x86_lapic_eoi_vector(periodic_source->Mapping,X86_TIMER_VECTOR)) return 0;
    ++periodic_total;
    if (++periodic_ticks[index] == 3) periodic_completion[index][0] = 1;
    return 1;
}

// ====== x86_scheduler_periodic_task ======

void x86_scheduler_periodic_task(uint32_t index)
{
    if (index >= 2 || !periodic_source || !x86_scheduler_task_valid()) kernel_scheduler_fail_stop();
    if (index == 0) {
        const X86_LAPIC_MAPPING *mapping = periodic_source->Mapping;
        // Intel SDM 078US Vol. 3A §11.5.4: +320h periodic mode, +380h starts counting.
        if (!x86_timer_confirm_write(mapping,0x320,(X86_LVT_TIMER_PERIODIC | X86_TIMER_VECTOR)) ||
            !x86_timer_confirm_write(mapping,0x380,periodic_source->Reload)) kernel_scheduler_fail_stop();
    }
    if (x86_scheduler_irq_poll(periodic_completion[index],periodic_source->Clock.Port,
        periodic_source->Clock.Mask,X86_PM_CLOCK_HZ / 2) != 1 || periodic_resumed[index] != 3)
        kernel_scheduler_fail_stop();
}

// ====== x86_scheduler_periodic_verified ======

int x86_scheduler_periodic_verified(void)
{
    return periodic_total == 6 && periodic_resumed[0] == 3 && periodic_resumed[1] == 3 &&
        periodic_quiet();
}

// ====== x86_scheduler_periodic_run ======

int x86_scheduler_periodic_run(const X86_LAPIC_TIMER *source)
{
    if (!source || periodic_source || !source->Reload || source->Error || !x86_irq_ready()) return 0;
    const X86_LAPIC_MAPPING *mapping = source->Mapping;
    periodic_source = source;
    if (!x86_timer_stop(mapping) || !x86_timer_confirm_write(mapping,0x3e0,3) || !periodic_quiet() ||
        !x86_irq_registry_bind(x86_irq_registry(),X86_TIMER_VECTOR,periodic_irq,periodic_quiet,&periodic_token) ||
        !x86_scheduler_run_periodic() || x86_irq_registry_deliveries(x86_irq_registry(),&periodic_token) != 6 ||
        !x86_irq_registry_unbind(x86_irq_registry(),&periodic_token) ||
        !x86_timer_confirm_write(mapping,0x3e0,source->OriginalDivide) ||
        !x86_timer_confirm_write(mapping,0x320,source->OriginalLvt) || !x86_lapic_idle(mapping)) return 0;
    periodic_source = 0;
    return kernel_log(0,"KERNEL_SCHEDULER_PERIODIC_OK");
}
#endif
