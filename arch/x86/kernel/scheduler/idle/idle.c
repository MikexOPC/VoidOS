#include "idle.h"
#include "kernel/scheduler/payload.h"
#include "../integration/control.h"
#include "../persistent/persistent.h"
#include "arch/x86/kernel/timer/lapic/runtime/runtime.h"
#include "arch/x86/kernel/interrupts/return/table.h"
#include "kernel/diagnostics/log.h"

static X86_LAPIC_TIMER *idle_source;
static uint32_t goal, posted, resumed, waits, slept, failure;
static uint32_t event_id, initial_done, repeat_done;
static uint64_t binding_generation;

// ====== x86_scheduler_idle_notify ======

int x86_scheduler_idle_notify(const X86_LAPIC_TIMER *source)
{
    if (!idle_source) return 1;
    if (source != idle_source || posted || x86_scheduler_index() != KERNEL_SCHEDULER_NONE ||
        !kernel_scheduler_context_valid() || !waits || source->Count != goal || source->Error ||
        source->Phase != X86_TIMER_PENDING ||
        kernel_scheduler_payload_push(event_id,(uint64_t)(uintptr_t)source,binding_generation,source->Count & 255) != 1) return 0;
    posted = 1;
    return 1;
}

// ====== x86_scheduler_idle_task ======

void x86_scheduler_idle_task(uint32_t index)
{
    KERNEL_EVENT_REPLY reply;
    uint64_t epoch;
    if (!idle_source || index >= 2 || !x86_scheduler_task_valid()) kernel_scheduler_fail_stop();
    reply = x86_scheduler_event_request(X86_TASK_OBSERVE,event_id,0);
    if (reply.outcome) kernel_scheduler_fail_stop();
    epoch = reply.epoch;
    reply = x86_scheduler_event_request(X86_TASK_WAIT,event_id,epoch);
    if (reply.outcome != KERNEL_EVENT_WAITING || reply.epoch != epoch || !posted || !waits ||
        idle_source->Count != goal || idle_source->Error || idle_source->Phase != X86_TIMER_PENDING ||
        resumed & (1U << index)) kernel_scheduler_fail_stop();
    if (index == 0) {
        if (kernel_scheduler_payload_pop(event_id,(uint64_t)(uintptr_t)idle_source,binding_generation,
                index,0) != KERNEL_PAYLOAD_DENIED ||
            kernel_scheduler_payload_pop(event_id,(uint64_t)(uintptr_t)idle_source,binding_generation,
                index,x86_scheduler_generation()) != (KERNEL_PAYLOAD_DATA | goal) ||
            kernel_scheduler_payload_pop(event_id,(uint64_t)(uintptr_t)idle_source,binding_generation,
                index,x86_scheduler_generation()) != KERNEL_PAYLOAD_EMPTY) kernel_scheduler_fail_stop();
    } else if (kernel_scheduler_payload_pop(event_id,(uint64_t)(uintptr_t)idle_source,binding_generation,
        index,x86_scheduler_generation()) != KERNEL_PAYLOAD_DENIED) kernel_scheduler_fail_stop();
    resumed |= 1U << index;
}

// ====== x86_scheduler_idle_wait ======

int x86_scheduler_idle_wait(void)
{
    KERNEL_EVENT_REPLY reply;
    uint32_t start;
    // Reject duplicate/inactive calls without overwriting the session's result.
    if (!idle_source || waits || posted) return 0;
    failure = 1; // Preconditions; no hardware mutation yet.
    if (!x86_timer_owner_valid(idle_source) || !x86_irq_ready() ||
        !kernel_scheduler_context_valid() ||
        x86_scheduler_index() != KERNEL_SCHEDULER_NONE || !x86_scheduler_idle_retained_valid() ||
        idle_source->Count + 1 != goal || idle_source->Consumed != idle_source->Count ||
        idle_source->Phase != X86_TIMER_READY || idle_source->Active || !x86_watchdog_valid()) return 0;
    waits = 1;
    failure = 2; // Arm source.
    start = x86_pm_read((unsigned short)idle_source->Clock.Port) & idle_source->Clock.Mask;
    if (!x86_timer_arm(idle_source)) return 0;
#if defined(KERNEL_TEST_SCHEDULER_FAULT) && KERNEL_TEST_SCHEDULER_FAULT == 8
    if (!x86_timer_stop(idle_source->Mapping)) return 0;
#elif defined(KERNEL_TEST_SCHEDULER_FAULT) && KERNEL_TEST_SCHEDULER_FAULT == 9
    if (goal == 3 && !x86_timer_stop(idle_source->Mapping)) return 0;
#endif
    // Existing hardware API owns STI/HLT/CLI and drains the independent HPET IRQ.
    // No exclusive Rust owner reference is held over this call.
    failure = 3; // Wait/ack independent watchdog.
    slept = x86_watchdog_wait(&idle_source->Count,goal,NULL);
    idle_source->DeadlinePm[goal-1] = x86_pm_elapsed(start,
        x86_pm_read((unsigned short)idle_source->Clock.Port) & idle_source->Clock.Mask,idle_source->Clock.Mask);
    if (!slept || slept > X86_WATCHDOG_WAKE_LIMIT || x86_watchdog_result() != X86_WATCHDOG_EVENT) return 0;
    failure = 4; // Verified delivery and dispatcher context.
    if (!posted || idle_source->Count != goal || idle_source->Error ||
        idle_source->Phase != X86_TIMER_PENDING || !kernel_scheduler_context_valid()) return 0;
    reply = kernel_scheduler_irq_drain();
    failure = 5; // Rust wake-all transaction.
    if (reply.reply.status != KERNEL_SCHEDULER_OK || reply.woken != 2 || reply.reply.ready != 2) return 0;
    if (!x86_watchdog_budget_end()) return 0;
    failure = 0;
    return 1;
}

// ====== x86_scheduler_idle_slept ======

uint32_t x86_scheduler_idle_slept(void) { return slept; }

// ====== x86_scheduler_idle_failure ======

uint32_t x86_scheduler_idle_failure(void) { return failure; }

// ====== x86_scheduler_idle_verified ======

int x86_scheduler_idle_verified(void)
{
    return idle_source && waits == 1 && posted == 1 && resumed == 3 && slept && slept <= X86_WATCHDOG_WAKE_LIMIT &&
        idle_source->Count == goal && !idle_source->Error && idle_source->Phase == X86_TIMER_PENDING;
}

// ====== x86_scheduler_timer_idle_tasks ======

static int idle_session_private(X86_LAPIC_TIMER *source, uint32_t count, uint32_t event,
    int (*run)(void))
{
    if (!source || idle_source || !run || event >= 8 || count < 1 || count > 2 || !x86_timer_owner_valid(source) ||
        !x86_irq_ready() || source->Count != count || source->Consumed != count || source->Error ||
        source->Active || source->Phase != X86_TIMER_READY || !x86_watchdog_valid()) return 0;
    {
        uint64_t previous = binding_generation;
        binding_generation = kernel_scheduler_irq_bind(event,(uint64_t)(uintptr_t)source);
        if (!binding_generation || (previous && binding_generation <= previous)) return 0;
        if (previous && kernel_scheduler_irq_post_bound(event,(uint64_t)(uintptr_t)source,previous)) return 0;
    }
    if (!kernel_scheduler_payload_open(event,(uint64_t)(uintptr_t)source,binding_generation,0)) return 0;
    idle_source = source; goal = count + 1; event_id = event;
    waits = posted = resumed = slept = failure = 0;
    if (!run() || !x86_scheduler_idle_verified()) return 0;
    // Hardware handler has returned; source is stopped/acknowledged, no task lease remains.
    failure = 6; // Quiescence and binding release; retain subscription on rejection.
    // PENDING retains logical Active until consume; the IRQ callback stopped hardware.
    if (!x86_timer_owner_valid(source) || !x86_lapic_idle(source->Mapping) ||
        !kernel_scheduler_payload_close(event,(uint64_t)(uintptr_t)source,binding_generation) ||
        !kernel_scheduler_irq_unbind(event,(uint64_t)(uintptr_t)source,binding_generation)) return 0;
    if (kernel_scheduler_irq_post_bound(event,(uint64_t)(uintptr_t)source,binding_generation)) return 0;
    idle_source = NULL;
    failure = 0;
    return 1;
}

// ====== x86_scheduler_timer_idle_tasks ======

int x86_scheduler_timer_idle_tasks(X86_LAPIC_TIMER *source)
{
    if (initial_done || !source || source->Count != 1 || source->Consumed != 1 ||
        !x86_timer_owner_valid(source)) return 0;
    initial_done = 1; // No retry after partial execution or a retained failure.
    if (!idle_session_private(source,1,3,x86_scheduler_run_idle) || !x86_scheduler_run_persistent()) return 0;
    return kernel_text("SCHEDULER_IDLE_SLEPT: ") && kernel_number(slept) &&
        kernel_text("\nSCHEDULER_IDLE_RESUMED: ") && kernel_number(resumed) && kernel_text("\n") &&
        kernel_log(0,"KERNEL_SCHEDULER_IDLE_OK");
}

// ====== x86_scheduler_timer_idle_repeat ======

int x86_scheduler_timer_idle_repeat(X86_LAPIC_TIMER *source)
{
    if (!initial_done || repeat_done || idle_source || !source || source->Count != 2 ||
        source->Consumed != 2 || !x86_timer_owner_valid(source)) return 0;
    repeat_done = 1;
    if (!idle_session_private(source,2,3,x86_scheduler_run_idle_repeat)) return 0;
    return kernel_text("SCHEDULER_IDLE_REPEAT_SLEPT: ") && kernel_number(slept) &&
        kernel_text("\nSCHEDULER_IDLE_REPEAT_RESUMED: ") && kernel_number(resumed) && kernel_text("\n") &&
        kernel_log(0,"KERNEL_SCHEDULER_IDLE_REPEAT_OK") &&
        kernel_log(0,"KERNEL_SCHEDULER_IRQ_BINDING_OK") &&
        kernel_log(0,"KERNEL_SCHEDULER_PAYLOAD_OK");
}
