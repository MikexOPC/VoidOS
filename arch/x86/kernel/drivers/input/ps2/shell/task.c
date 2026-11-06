#include "task.h"
#include "../runtime/session.h"
#include "arch/x86/kernel/scheduler/integration/control.h"
#include "kernel/diagnostics/log.h"
#ifdef KERNEL_CONSOLE_SHELL
#include "arch/x86/kernel/drivers/video/framebuffer/console/runtime.h"
#endif

// ====== event_private ======

static KERNEL_EVENT_REPLY event_private(uint32_t reason, uint32_t id, uint64_t token)
{
    KERNEL_EVENT_REPLY reply = x86_scheduler_event_request(reason,id,token);
    if (reply.reply.status != KERNEL_SCHEDULER_OK) kernel_scheduler_fail_stop();
    return reply;
}

// ====== x86_ps2_shell_submit ======

int x86_ps2_shell_submit(void)
{
    X86_PS2_SESSION *owner = x86_ps2_session();
    KERNEL_PS2_COMMAND command;
    if (x86_scheduler_index() != 0 || !x86_scheduler_driver_task_valid()) return 0;
    command.Line = owner->Line; command.Truncated = owner->LineTruncated;
#ifdef KERNEL_CONSOLE_SHELL
    if (!owner->Stop && !x86_console_submit(owner)) { kernel_ps2_command_clear(&command); return 0; }
#endif
    while (!owner->Stop) {
        KERNEL_EVENT_REPLY observed = event_private(X86_TASK_OBSERVE,6,0);
        uint32_t result = kernel_ps2_command_push(&owner->Commands,&command);
        if (result == KERNEL_PS2_QUEUE_OK) {
            (void)event_private(X86_TASK_SIGNAL,5,0);
            kernel_ps2_command_clear(&command); return 1;
        }
        if (result != KERNEL_PS2_QUEUE_FULL) return 0;
        (void)event_private(X86_TASK_WAIT,6,observed.epoch);
    }
    kernel_ps2_command_clear(&command); return 1;
}

// ====== x86_ps2_shell_close ======

void x86_ps2_shell_close(void)
{
    if (x86_scheduler_index() != 0 || !x86_scheduler_driver_task_valid() ||
        !kernel_ps2_command_close(&x86_ps2_session()->Commands)) kernel_scheduler_fail_stop();
    (void)event_private(X86_TASK_SIGNAL,5,0);
}

// ====== x86_ps2_shell_task ======

void x86_ps2_shell_task(void)
{
    X86_PS2_SESSION *owner = x86_ps2_session();
    KERNEL_PS2_COMMAND command = {0};
    if (x86_scheduler_index() != 2 || !x86_scheduler_driver_task_valid() ||
        !kernel_log(1,"SHELL_TASK2_READY")) kernel_scheduler_fail_stop();
    for (;;) {
        KERNEL_EVENT_REPLY observed = event_private(X86_TASK_OBSERVE,5,0);
        uint32_t result = kernel_ps2_command_pop(&owner->Commands,&command);
        if (result == KERNEL_PS2_QUEUE_CLOSED) break;
        if (result == KERNEL_PS2_QUEUE_EMPTY) {
            (void)event_private(X86_TASK_WAIT,5,observed.epoch); continue;
        }
        if (result != KERNEL_PS2_QUEUE_OK) kernel_scheduler_fail_stop();
        (void)event_private(X86_TASK_SIGNAL,6,0);
        if (!owner->Stop) {
            KERNEL_PS2_SHELL_STATUS status = {owner->Bytes,owner->Keys,owner->Lines};
            int executed = kernel_ps2_shell_execute(&command,&status);
            if (!executed || owner->ShellCommands == ~0U) kernel_scheduler_fail_stop();
            ++owner->ShellCommands;
            if (executed == 2) owner->Stop = 1;
        }
        kernel_ps2_command_clear(&command);
        x86_scheduler_yield();
    }
    kernel_ps2_command_clear(&command);
    owner->ShellDone = 1;
    if (!kernel_log(0,"SHELL_TASK2_CLOSED_EMPTY")) kernel_scheduler_fail_stop();
}
