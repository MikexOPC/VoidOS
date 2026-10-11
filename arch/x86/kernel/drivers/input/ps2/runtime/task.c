#include "session.h"
#include "kernel/drivers/input/ps2/decoder.h"
#include "kernel/input/keyboard/text.h"
#include "kernel/scheduler/payload.h"
#include "arch/x86/kernel/scheduler/integration/control.h"
#include "arch/x86/kernel/scheduler/irq/bridge.h"
#include "kernel/diagnostics/log.h"
#include "../shell/task.h"
#ifdef KERNEL_CONSOLE_SHELL
#include "arch/x86/kernel/drivers/video/framebuffer/console/runtime.h"
#endif
#ifdef KERNEL_TEST_CONSOLE_DRAW
#include "test/kernel/console/video/active/worker.h"
#endif

// ====== pop_private ======

static BOOT_U64 pop_private(void)
{
    X86_PS2_SESSION *owner = x86_ps2_session();
    return kernel_scheduler_payload_pop(4,(BOOT_U64)owner,owner->Binding,0,x86_scheduler_generation());
}

// ====== line_emit_private ======

static int line_emit_private(const KERNEL_PS2_LINE *line)
{
    char chunk[129];
    if (!line || line->Length > KERNEL_PS2_LINE_CAPACITY) return 0;
    // kernel_text requires NUL inside its 256-byte scan. A full256-byte
    // line therefore needs bounded chunks, preserving the byte stream.
    for (uint32_t offset = 0; offset < line->Length;) {
        uint32_t count = line->Length - offset;
        if (count > 128) count = 128;
        for (uint32_t index = 0; index < count; ++index) chunk[index] = line->Data[offset + index];
        chunk[count] = 0;
        if (!kernel_text(chunk)) return 0;
        offset += count;
    }
    return 1;
}

// ====== line_private ======

static int line_private(uint32_t scalar)
{
    X86_PS2_SESSION *owner = x86_ps2_session();
    uint32_t status = kernel_ps2_line_step(&owner->Line,scalar);
    if (status == KERNEL_PS2_LINE_INVALID) return 0;
    if (status == KERNEL_PS2_LINE_FULL) {
        if (owner->LineFull == ~0U) return 0;
        ++owner->LineFull;
        owner->LineTruncated = 1;
        return kernel_log(2,"PS2_LINE_FULL");
    }
#ifdef KERNEL_CONSOLE_SHELL
    if (!x86_console_edit(owner)) return 0;
#endif
    if (status != KERNEL_PS2_LINE_SUBMIT) return 1;
    if (owner->Lines == ~0U || !kernel_text("PS2_LINE_READY: ") || !line_emit_private(&owner->Line) ||
        !kernel_text("\nPS2_LINE_LENGTH: ") || !kernel_number(owner->Line.Length) || !kernel_text("\n")) return 0;
    if (!x86_ps2_shell_submit()) return 0;
    ++owner->Lines;
    kernel_ps2_line_clear(&owner->Line);
    owner->LineTruncated = 0;
    return 1;
}

// ====== text_private ======

static int text_private(uint32_t event)
{
    X86_PS2_SESSION *owner = x86_ps2_session();
    BOOT_U64 result = kernel_ps2_text(owner->TextState,event), encoded;
    uint32_t scalar = (uint32_t)result, length;
    char buffer[5] = {0};
    if (owner->Stop) return 1;
    owner->TextState = (uint32_t)(result >> 32);
    if (scalar == KERNEL_PS2_TEXT_INVALID) return 0;
    if (!scalar) return 1;
    if (!line_private(scalar)) return 0;
    if (scalar == KERNEL_PS2_TEXT_BACKSPACE) return kernel_log(1,"PS2_EDIT_BACKSPACE");
    if (scalar == KERNEL_PS2_TEXT_TAB) return kernel_log(1,"PS2_EDIT_TAB");
    if (scalar == KERNEL_PS2_TEXT_ENTER) return kernel_log(1,"PS2_EDIT_ENTER");
    encoded = kernel_ps2_utf8(scalar); length = (uint32_t)(encoded >> 32);
    // No terminal control injection: editing actions remain typed log events.
    if (!length || length > 4 || scalar < 32 || scalar == 127 || owner->TextCount == ~0U) return 0;
    for (uint32_t index = 0; index < length; ++index) buffer[index] = (char)(encoded >> (index * 8));
    ++owner->TextCount;
    return kernel_text("PS2_TEXT: ") && kernel_text(buffer) && kernel_text("\nPS2_UNICODE: ") &&
        kernel_number(scalar) && kernel_text("\n");
}

// ====== consumer_private ======

static void consumer_private(void)
{
    X86_PS2_SESSION *owner = x86_ps2_session();
    owner->ConsumerGeneration = x86_scheduler_generation();
    for (;;) {
        BOOT_U64 value = pop_private(), decoded;
        if (value == KERNEL_PAYLOAD_EMPTY) {
            KERNEL_EVENT_REPLY observed;
            if (owner->Stop && owner->Quiet) break;
            owner->Ready = 0;
            observed = x86_scheduler_event_request(X86_TASK_OBSERVE,4,0);
            value = pop_private();
            if (value == KERNEL_PAYLOAD_EMPTY) {
                if (owner->Stop && owner->Quiet) break;
                (void)x86_scheduler_event_request(X86_TASK_WAIT,4,observed.epoch);
                continue;
            }
        }
        if ((value & 0xffffffffULL) == KERNEL_PAYLOAD_LOSS) {
            owner->Decoder = owner->TextState = 0; ++owner->Losses; owner->Failed = 1; owner->Stop = 1;
            kernel_ps2_line_clear(&owner->Line);
            x86_scheduler_yield(); continue;
        }
        if ((value & ~255ULL) != KERNEL_PAYLOAD_DATA) kernel_scheduler_fail_stop();
        decoded = kernel_ps2_decode(owner->Decoder,(uint32_t)value & 255);
        owner->Decoder = (uint32_t)(decoded >> 32);
        if ((uint32_t)decoded == KERNEL_PS2_INVALID) {
            kernel_ps2_line_clear(&owner->Line);
            owner->TextState = 0; owner->Failed = 1; owner->Stop = 1; x86_scheduler_yield(); continue;
        }
        if ((uint32_t)decoded == KERNEL_PS2_PENDING) continue;
        if (!text_private((uint32_t)decoded)) kernel_scheduler_fail_stop();
        if (((uint32_t)decoded & 0xffff0000U) == KERNEL_PS2_RELEASE) ++owner->Releases;
        else {
            ++owner->Keys;
            if (!kernel_text("PS2_KEY: ") || !kernel_number((uint32_t)decoded & 65535) || !kernel_text("\n"))
                kernel_scheduler_fail_stop();
            // Set2 Esc=76h. Explicit v0 stop key, independent of text/layout.
            if (((uint32_t)decoded & 65535) == 0x76) {
                owner->Stop = 1; x86_scheduler_yield();
            }
        }
        // Bound each consumer burst; allow companion and pending IRQ progress.
        x86_scheduler_yield();
    }
    kernel_ps2_line_clear(&owner->Line);
    owner->LineTruncated = 0;
    x86_ps2_shell_close();
    owner->TextState = 0; owner->ConsumerDone = 1;
}

// ====== worker_private ======

static void worker_private(void)
{
    X86_PS2_SESSION *owner = x86_ps2_session();
    owner->WorkerGeneration = x86_scheduler_generation();
    if (!kernel_log(1,"KERNEL_PS2_TASK_POLL_READY")) kernel_scheduler_fail_stop();
    for (;;) {
        uint32_t poll_ticks = X86_PM_CLOCK_HZ / 10;
#ifdef KERNEL_CONSOLE_SHELL
        int pending = x86_console_pending(owner);
        if (pending < 0) kernel_scheduler_fail_stop();
        if (owner->Quiet && !pending) break;
        if (!x86_console_flush(owner)) kernel_scheduler_fail_stop();
        if (owner->Quiet) { x86_scheduler_yield(); continue; }
        if (x86_console_pending(owner)) poll_ticks = X86_PM_CLOCK_HZ / 1000;
#else
        if (owner->Quiet) break;
#endif
        KERNEL_EVENT_REPLY observed = x86_scheduler_event_request(X86_TASK_OBSERVE,4,0);
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
        if (!x86_scheduler_irq_poll(&owner->Ready,owner->Clock.Port,owner->Clock.Mask,poll_ticks) ||
            owner->Error || !x86_scheduler_task_valid()) kernel_scheduler_fail_stop();
#ifdef KERNEL_CONSOLE_SHELL
        if (x86_console_pending(owner)) { x86_scheduler_yield(); continue; }
#endif
        if (!owner->Ready && !owner->Quiet) (void)x86_scheduler_event_request(X86_TASK_WAIT,4,observed.epoch);
        else x86_scheduler_yield();
    }
    owner->WorkerDone = 1;
}

// ====== x86_ps2_task ======

void x86_ps2_task(uint32_t index)
{
    if (!x86_ps2_session()->Active || !x86_scheduler_task_valid() || index > 2) kernel_scheduler_fail_stop();
    if (index == 0) consumer_private();
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
    return kernel_scheduler_irq_drain().reply.status == KERNEL_SCHEDULER_OK ? 1 : idle_failure(3);
}
