#include "session.h"
#include "kernel/drivers/input/ps2/decoder.h"
#include "kernel/input/keyboard/text.h"
#include "kernel/scheduler/payload.h"
#include "arch/x86/kernel/scheduler/integration/control.h"
#ifdef KERNEL_AP_TASKS
#include "arch/x86/kernel/scheduler/remote/remote.h"
#endif
#include "arch/x86/kernel/scheduler/irq/bridge.h"
#include "kernel/diagnostics/log.h"
#include "../shell/task.h"
#ifdef KERNEL_CONSOLE_SHELL
#include "arch/x86/kernel/drivers/video/framebuffer/console/runtime.h"
#endif
#ifdef KERNEL_TEST_CONSOLE_DRAW
#include "test/kernel/console/video/active/worker.h"
#endif

#ifdef KERNEL_CONSOLE_SHELL
// ====== console_flush_private ======

static int console_flush_private(X86_PS2_SESSION *owner)
{
    /* ACPI6.5 §4.8.3.3: existing validated PM_TMR port, 24/32-bit mask;
       measure the failed operation without changing the renderer deadline. */
    uint32_t start = x86_pm_read((unsigned short)owner->Clock.Port) & owner->Clock.Mask;
    if (x86_console_flush(owner)) return 1;
    kernel_text("PS2_CONSOLE_FAILED_PM: ");
    kernel_number((x86_pm_read((unsigned short)owner->Clock.Port) - start) & owner->Clock.Mask);
    kernel_text("\n"); kernel_log(3,"PS2_INPUT_FAILED_CONSOLE_FLUSH");
    return 0;
}
#endif

// ====== worker_failure_private ======

static void worker_failure_private(uint32_t stage, const X86_PS2_SESSION *owner)
{
    kernel_text("PS2_WORKER_FAILURE_STAGE: "); kernel_number(stage);
    kernel_text("\nPS2_WORKER_FAILURE_ERROR: "); kernel_number(owner->Error);
    kernel_text("\nPS2_WORKER_FAILURE_LOSSES: "); kernel_number(owner->Losses);
    kernel_text("\n"); kernel_log(3, "PS2_INPUT_FAILED_WORKER");
    kernel_scheduler_fail_stop();
}

// ====== worker_private ======

static void worker_private(void)
{
    X86_PS2_SESSION *owner = x86_ps2_session();
    owner->WorkerGeneration = x86_scheduler_generation();
#ifdef KERNEL_PS2_MOUSE
    const uint32_t worker_event = 6;
#else
    const uint32_t worker_event = 4;
#endif
    if (!kernel_log(1,"KERNEL_PS2_TASK_POLL_READY")) kernel_scheduler_fail_stop();
    for (;;) {
        uint32_t poll_ticks = X86_PM_CLOCK_HZ / 10;
#if defined(KERNEL_CONSOLE_SHELL) && defined(KERNEL_PS2_MOUSE)
        poll_ticks = X86_PM_CLOCK_HZ / 1000;
#endif
#ifdef KERNEL_CONSOLE_SHELL
        int pending = x86_console_pending(owner);
        if (pending < 0) worker_failure_private(1, owner);
        if (owner->Quiet && !pending) break;
        if (!console_flush_private(owner)) worker_failure_private(2, owner);
        if (owner->Quiet) { x86_scheduler_yield(); continue; }
        if (x86_console_pending(owner)) poll_ticks = X86_PM_CLOCK_HZ / 1000;
        // G5 V1-A: nothing to draw, so the paging audit (at most once per second) runs here.
        else if (!x86_console_audit(owner)) worker_failure_private(6, owner);
#else
        if (owner->Quiet) break;
#endif
        KERNEL_EVENT_REPLY observed = x86_scheduler_event_request(X86_TASK_OBSERVE,worker_event,0);
#ifdef KERNEL_CONSOLE_SHELL
        if (owner->Quiet) continue;
#else
        if (owner->Quiet) break;
#endif
#ifdef KERNEL_TEST_CONSOLE_DRAW
        if (!kernel_console_active_worker(owner)) kernel_scheduler_fail_stop();
#endif
        // Reuse the audited Assembly IF=1 window and SIMD/GPR preservation
        // checks. Bounded100ms bring-up window; empty input then parks.
        if (!x86_scheduler_irq_poll(&owner->Ready,owner->Clock.Port,owner->Clock.Mask,poll_ticks))
            worker_failure_private(3, owner);
        if (owner->Error) worker_failure_private(4, owner);
        if (!x86_scheduler_driver_task_valid()) worker_failure_private(5, owner);
#ifdef KERNEL_CONSOLE_SHELL
        if (x86_console_pending(owner)) { x86_scheduler_yield(); continue; }
#endif
        if (!owner->Ready && !owner->Quiet) (void)x86_scheduler_event_request(X86_TASK_WAIT,worker_event,observed.epoch);
        else x86_scheduler_yield();
    }
    owner->WorkerDone = 1;
}

// ====== x86_ps2_task ======

void x86_ps2_task(uint32_t index)
{
    if (!x86_ps2_session()->Active || !x86_scheduler_driver_task_valid()) kernel_scheduler_fail_stop();
#ifdef KERNEL_PS2_MOUSE
    if (index == 3) { x86_ps2_mouse_task(); return; }
#endif
    if (index > 2) kernel_scheduler_fail_stop();
    if (index == 0) x86_ps2_consume();
    else if (index == 1) worker_private();
    else x86_ps2_shell_task();
}

// ====== idle_failure ======

static int idle_failure(uint32_t stage)
{
    const X86_DISPATCH_STATS *stats = x86_scheduler_session_stats();
    kernel_text("PS2_IDLE_FAILURE_STAGE: "); kernel_number(stage);
    kernel_text("\nPS2_DISPATCH_STEPS: "); kernel_number(stats ? stats->steps : UINT32_MAX);
    kernel_text("\nPS2_DISPATCH_IDLE: "); kernel_number(stats ? stats->idle_calls : UINT32_MAX);
    kernel_text("\nPS2_WATCHDOG_FAILURE: "); kernel_number(x86_watchdog_failure());
    kernel_text("\nPS2_WATCHDOG_WAKE_DETAIL: "); kernel_number(x86_watchdog_wake_detail());
    kernel_text("\nPS2_WATCHDOG_ARM_DETAIL: "); kernel_number(x86_watchdog_arm_detail());
    kernel_text("\nPS2_WATCHDOG_ARM_REGISTERS: "); kernel_number(x86_watchdog_arm_registers());
    kernel_text("\nPS2_WATCHDOG_START: "); kernel_number(x86_watchdog_state()->Start);
    kernel_text("\nPS2_WATCHDOG_TARGET: "); kernel_number(x86_watchdog_state()->Target);
    kernel_text("\nPS2_WATCHDOG_TICKS: "); kernel_number(x86_watchdog_state()->Ticks);
    kernel_text("\nPS2_WATCHDOG_COUNT: "); kernel_number(x86_watchdog_state()->Count);
    kernel_text("\nPS2_WATCHDOG_RESULT: "); kernel_number(x86_watchdog_result()); kernel_text("\n");
    return 0;
}

// ====== x86_ps2_idle ======

int x86_ps2_idle(void)
{
    X86_PS2_SESSION *owner = x86_ps2_session();
    if (!owner->Active || owner->Quiet || owner->Idle == ~0U || !x86_scheduler_retained_valid()) return idle_failure(1);
    if (!x86_watchdog_idle(&owner->Ready)) return idle_failure(2);
    ++owner->Idle;
#ifdef KERNEL_AP_TASKS
    x86_remote_service();
#endif
    return kernel_scheduler_irq_drain().reply.status == KERNEL_SCHEDULER_OK ? 1 : idle_failure(3);
}
