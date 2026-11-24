#include "../runtime/session.h"
#include "kernel/scheduler/payload.h"
#include "arch/x86/kernel/scheduler/integration/control.h"
#include "kernel/diagnostics/log.h"
#ifdef KERNEL_CONSOLE_SHELL
#include "arch/x86/kernel/drivers/video/framebuffer/console/runtime.h"
#endif
#ifdef KERNEL_USER_PROCESS
#include "kernel/gui/gui.h"
#include "arch/x86/kernel/drivers/video/virtio/direct.h"
#endif

// ====== pop_private ======

static BOOT_U64 pop_private(X86_PS2_SESSION *owner, int reset_ready)
{
    uint64_t saved_if = kernel_preempt_section_enter();
    X86_PS2_MOUSE *mouse = &owner->Mouse;
    BOOT_U64 value = kernel_scheduler_payload_pop(6,(BOOT_U64)mouse,mouse->Binding,3,x86_scheduler_generation());
    if (reset_ready && value == KERNEL_PAYLOAD_EMPTY && !(owner->Stop && owner->Quiet)) owner->Ready = 0;
    kernel_preempt_section_leave(saved_if);
    return value;
}

// ====== delta_private ======

#ifndef KERNEL_INPUT_INTERACTIVE
static int delta_private(int32_t value)
{
    if (value < -256 || value > 255) return 0;
    return (value >= 0 || kernel_text("-")) && kernel_number((BOOT_U64)(value < 0 ? -value : value));
}
#endif

// ====== framing_error_private ======

/* A lost or malformed byte only desynchronises the 3-byte packet frame: the decoder
   returns to its header state and each rejected byte is skipped (a header needs bit3,
   so the frame realigns within one packet). Fail the session only when no valid
   event arrives for X86_PS2_MOUSE_RESYNC_LIMIT consecutive errors. */
static void framing_error_private(X86_PS2_SESSION *owner, X86_PS2_MOUSE *mouse)
{
    kernel_ps2_mouse_reset(&mouse->Decoder);
    if (mouse->Streak != UINT32_MAX) ++mouse->Streak;
    if (mouse->Streak > X86_PS2_MOUSE_RESYNC_LIMIT) owner->Failed = owner->Stop = 1;
}

// ====== x86_ps2_mouse_task ======

void x86_ps2_mouse_task(void)
{
    X86_PS2_SESSION *owner = x86_ps2_session();
    X86_PS2_MOUSE *mouse = &owner->Mouse;
    uint32_t burst = 0;
    mouse->Generation = x86_scheduler_generation();
    for (;;) {
        BOOT_U64 value = pop_private(owner, 1);
        KERNEL_PS2_MOUSE_EVENT event;
        KERNEL_PS2_MOUSE_RESULT result;
        if (value == KERNEL_PAYLOAD_EMPTY) {
            burst = 0;
            KERNEL_EVENT_REPLY observed;
            if (owner->Stop && owner->Quiet) break;
            observed = x86_scheduler_event_request(X86_TASK_OBSERVE,6,0);
            value = pop_private(owner, 0);
            if (value == KERNEL_PAYLOAD_EMPTY) {
                if (owner->Stop && owner->Quiet) break;
                (void)x86_scheduler_event_request(X86_TASK_WAIT,6,observed.epoch);
                continue;
            }
        }
        if ((uint32_t)value == KERNEL_PAYLOAD_LOSS) {
            if (mouse->Losses == UINT32_MAX) kernel_scheduler_fail_stop();
            ++mouse->Losses;
            framing_error_private(owner, mouse);
            burst = 0;
            x86_scheduler_yield();
            continue;
        }
        if ((value & ~255ULL) != KERNEL_PAYLOAD_DATA) kernel_scheduler_fail_stop();
        result = kernel_ps2_mouse_feed(&mouse->Decoder,(uint32_t)value & 255,&event);
        if (result == KERNEL_PS2_MOUSE_WAIT) continue;
        if (result == KERNEL_PS2_MOUSE_REJECTED) {
            if (mouse->Rejected != UINT32_MAX) ++mouse->Rejected;
            framing_error_private(owner, mouse);
        } else if (result == KERNEL_PS2_MOUSE_SATURATED) {
            /* Well-framed packet whose X/Y overflow bits are set (a fast real mouse);
               design: consume the three bytes and drop the whole event. Frame sync
               is intact, so the error streak is left untouched. */
            if (mouse->Saturated != UINT32_MAX) ++mouse->Saturated;
        } else if (result == KERNEL_PS2_MOUSE_EVENT_READY) {
            mouse->Streak = 0;
            if (mouse->Events == UINT32_MAX) kernel_scheduler_fail_stop();
            ++mouse->Events;
            mouse->Buttons = event.Buttons;
#ifdef KERNEL_USER_PROCESS
            if (kernel_gui_global() && kernel_gui_display_owner(kernel_gui_global()) != 0) {
                x86_vgpu_direct_cursor_move_delta(event.DeltaX, event.DeltaY);
                (void)kernel_gui_input_push_mouse(kernel_gui_global(), event.DeltaX, event.DeltaY, event.Buttons);
            } else {
#endif
#ifndef KERNEL_INPUT_INTERACTIVE
                if (!kernel_text("MOUSE_EVENT: ") ||
                    !delta_private(event.DeltaX) || !kernel_text(" ") || !delta_private(event.DeltaY) ||
                    !kernel_text(" ") || !kernel_number(event.Buttons) || !kernel_text("\n")) kernel_scheduler_fail_stop();
#endif
#ifdef KERNEL_CONSOLE_SHELL
                if (!x86_console_mouse(owner, event.DeltaX, event.DeltaY, event.Buttons)) kernel_scheduler_fail_stop();
#endif
#ifdef KERNEL_USER_PROCESS
            }
#endif
        }
        if (owner->Stop || ++burst == 32) { burst = 0; x86_scheduler_yield(); }
    }
    kernel_ps2_mouse_reset(&mouse->Decoder);
    mouse->Buttons = 0;
    mouse->Done = 1;
}
