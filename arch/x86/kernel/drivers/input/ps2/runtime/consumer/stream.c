#include "../session.h"
#include "kernel/drivers/input/ps2/decoder.h"
#include "kernel/input/keyboard/text.h"
#include "kernel/scheduler/payload.h"
#include "arch/x86/kernel/scheduler/integration/control.h"
#include "arch/x86/kernel/scheduler/irq/bridge.h"
#include "kernel/diagnostics/log.h"
#include "../../shell/task.h"
#ifdef KERNEL_CONSOLE_SHELL
#include "arch/x86/kernel/drivers/video/framebuffer/console/runtime.h"
#endif
#ifdef KERNEL_USER_PROCESS
#include "kernel/gui/gui.h"
#endif
#ifdef KERNEL_TEST_CONSOLE_DRAW
#include "test/kernel/console/video/active/worker.h"
#endif

// ====== pop_private ======

static BOOT_U64 pop_private(void)
{
    X86_PS2_SESSION *owner = x86_ps2_session();
    BOOT_U64 value = kernel_scheduler_payload_pop(4,(BOOT_U64)owner,owner->Binding,0,x86_scheduler_generation());
    if (value != KERNEL_PAYLOAD_EMPTY && (BOOT_U32)value != KERNEL_PAYLOAD_LOSS &&
        (value & ~255ULL) != KERNEL_PAYLOAD_DATA) kernel_scheduler_fail_stop();
    return value;
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
#ifdef KERNEL_USER_PROCESS
    if (kernel_gui_global() && kernel_gui_display_owner(kernel_gui_global()) != 0) {
        if (!kernel_gui_input_push_key(kernel_gui_global(), event, scalar, owner->TextState)) {
            if (owner->GuiKeyDrops != UINT32_MAX)
                ++owner->GuiKeyDrops;
        }
        return 1;
    }
#endif
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

// ====== resync_private ======

// Recoverable keyboard fault (B2): reset the stream state (policy in interrupt.c), then refresh the
// console edit line so the screen matches the now empty line.
static int resync_private(X86_PS2_SESSION *owner, int verdict)
{
    int proceed = x86_ps2_fault_resync(owner, verdict);
#ifdef KERNEL_CONSOLE_SHELL
    if (!x86_console_edit(owner)) { owner->Failed = 1; owner->Stop = 1; return 0; }
#endif
    return proceed;
}

// ====== cancel_private ======

static int cancel_private(X86_PS2_SESSION *owner)
{
    kernel_ps2_line_clear(&owner->Line);
    owner->LineTruncated = 0;
#ifdef KERNEL_CONSOLE_SHELL
    if (!x86_console_edit(owner)) return 0;
#endif
    return kernel_log(1,"PS2_EDIT_CANCEL");
}

// ====== x86_ps2_consume ======

void x86_ps2_consume(void)
{
    X86_PS2_SESSION *owner = x86_ps2_session();
    owner->ConsumerGeneration = x86_scheduler_generation();
    for (;;) {
        uint64_t saved_if = kernel_preempt_section_enter();
        BOOT_U64 value = pop_private(), decoded;
        int verdict = x86_ps2_fault_poll(owner);
        if (verdict && !x86_ps2_fault_discard(owner)) kernel_scheduler_fail_stop();
        if (!verdict && value == KERNEL_PAYLOAD_EMPTY && !(owner->Stop && owner->Quiet)) owner->Ready = 0;
        kernel_preempt_section_leave(saved_if);
        if (verdict) {
            if (!resync_private(owner, verdict)) x86_scheduler_yield();
            continue;
        }
        if (value == KERNEL_PAYLOAD_EMPTY) {
            KERNEL_EVENT_REPLY observed;
            if (owner->Stop && owner->Quiet) break;
            observed = x86_scheduler_event_request(X86_TASK_OBSERVE,4,0);
            saved_if = kernel_preempt_section_enter();
            value = pop_private();
            kernel_preempt_section_leave(saved_if);
            if (value == KERNEL_PAYLOAD_EMPTY) {
                if (owner->Stop && owner->Quiet) break;
                (void)x86_scheduler_event_request(X86_TASK_WAIT,4,observed.epoch);
                continue;
            }
        }
        if ((value & 0xffffffffULL) == KERNEL_PAYLOAD_LOSS) {
            if (!resync_private(owner, x86_ps2_fault_note(owner))) x86_scheduler_yield();
            continue;
        }
        if ((value & ~255ULL) != KERNEL_PAYLOAD_DATA) kernel_scheduler_fail_stop();
        decoded = kernel_ps2_decode(owner->Decoder,(uint32_t)value & 255);
        owner->Decoder = (uint32_t)(decoded >> 32);
        if ((uint32_t)decoded == KERNEL_PS2_INVALID) {
            if (!resync_private(owner, x86_ps2_fault_note(owner))) x86_scheduler_yield();
            continue;
        }
        if ((uint32_t)decoded == KERNEL_PS2_PENDING) continue;
        x86_ps2_fault_valid(owner);
        if (!text_private((uint32_t)decoded)) kernel_scheduler_fail_stop();
        if (((uint32_t)decoded & 0xffff0000U) == KERNEL_PS2_RELEASE) ++owner->Releases;
        else {
            ++owner->Keys;
            if (!kernel_text("PS2_KEY: ") || !kernel_number((uint32_t)decoded & 65535) || !kernel_text("\n"))
                kernel_scheduler_fail_stop();
            if (((uint32_t)decoded & 65535) == 0x76 && !owner->Stop && !cancel_private(owner))
                kernel_scheduler_fail_stop();
        }
        // Bound each consumer burst; allow companion and pending IRQ progress.
        x86_scheduler_yield();
    }
    kernel_ps2_line_clear(&owner->Line);
    owner->LineTruncated = 0;
    x86_ps2_shell_close();
    owner->TextState = 0; owner->ConsumerDone = 1;
}

