#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "arch/x86/kernel/drivers/input/ps2/runtime/session.h"
#include "arch/x86/kernel/drivers/input/ps2/shell/task.h"
#include "arch/x86/kernel/scheduler/integration/control.h"
static X86_PS2_SESSION owner;
static uint32_t actor, waits, data_signals, space_signals, cancel, feed;
static uint64_t epoch = 1;
#ifdef KERNEL_CONSOLE_SHELL
static uint32_t submitted, submit_failed;

// ====== console_commit_before_signal ======

int x86_console_submit(X86_PS2_SESSION *session)
{
    assert(session == &owner && actor == 0 && !owner.Stop);
    ++submitted;
    return !submit_failed;
}
#endif

// ====== scheduler_boundary_mocks ======

X86_PS2_SESSION *x86_ps2_session(void) { return &owner; }
uint32_t x86_scheduler_index(void) { return actor; }
int x86_scheduler_task_valid(void) { return 1; }
__declspec(noreturn) void kernel_scheduler_fail_stop(void) { abort(); }
int kernel_log(unsigned level, const char *text) { (void)level; (void)text; return 1; }
int kernel_ps2_shell_execute(const KERNEL_PS2_COMMAND *command, const KERNEL_PS2_SHELL_STATUS *status)
{ assert(actor == 2 && command->Line.Length == 4 && status); return 1; }
void x86_scheduler_yield(void) { }
KERNEL_EVENT_REPLY x86_scheduler_event_request(uint32_t reason, uint32_t id, uint64_t token)
{
    KERNEL_EVENT_REPLY reply = {0};
    reply.reply.status = KERNEL_SCHEDULER_OK; reply.epoch = epoch;
    if (reason == X86_TASK_SIGNAL) {
#ifdef KERNEL_CONSOLE_SHELL
        if (id == 5 && data_signals == 0) assert(submitted == 1);
#endif
        if (id == 5) ++data_signals; else { assert(id == 6); ++space_signals; }
    } else if (reason == X86_TASK_WAIT) {
        KERNEL_PS2_COMMAND command = {0};
        assert(token == epoch); ++waits; ++epoch;
        if (id == 6) {
            assert(actor == 0);
            if (cancel) owner.Stop = 1;
            else assert(kernel_ps2_command_pop(&owner.Commands,&command) == KERNEL_PS2_QUEUE_OK);
        } else {
            assert(id == 5 && actor == 2 && feed);
            memcpy(command.Line.Data,"help",5); command.Line.Length = 4;
            assert(kernel_ps2_command_push(&owner.Commands,&command) == KERNEL_PS2_QUEUE_OK);
            assert(kernel_ps2_command_close(&owner.Commands));
        }
    } else assert(reason == X86_TASK_OBSERVE && (id == 5 || id == 6));
    return reply;
}

// ====== fill_private ======

static void fill_private(void)
{
    KERNEL_PS2_COMMAND command = {0};
    for (uint32_t index = 0; index < 4; ++index) {
        command.Line.Data[0] = (char)('a' + index); command.Line.Length = 1;
        assert(kernel_ps2_command_push(&owner.Commands,&command) == KERNEL_PS2_QUEUE_OK);
    }
    owner.Line.Data[0] = 'e'; owner.Line.Length = 1;
}

// ====== full_backpressure_cancel_and_eof ======

int main(void)
{
    KERNEL_PS2_COMMAND command;
    fill_private();
    assert(x86_ps2_shell_submit());
    assert(waits == 1 && data_signals == 1 && owner.Line.Length == 1 && owner.Line.Data[0] == 'e');
    for (uint32_t index = 0; index < 4; ++index) {
        assert(kernel_ps2_command_pop(&owner.Commands,&command) == KERNEL_PS2_QUEUE_OK);
        assert(command.Line.Data[0] == (char)('b' + index));
    }
    x86_ps2_shell_close(); assert(data_signals == 2 && kernel_ps2_command_closed_empty(&owner.Commands));
    owner = (X86_PS2_SESSION){0}; cancel = 1; fill_private();
    assert(x86_ps2_shell_submit() && owner.Stop && owner.Line.Length == 1);
    for (uint32_t index = 0; index < 4; ++index)
        assert(kernel_ps2_command_pop(&owner.Commands,&command) == KERNEL_PS2_QUEUE_OK);
    x86_ps2_shell_close(); assert(kernel_ps2_command_closed_empty(&owner.Commands));
#ifdef KERNEL_CONSOLE_SHELL
    assert(submitted == 1);
    owner = (X86_PS2_SESSION){0}; cancel = 0; submit_failed = 1;
    owner.Line.Data[0] = 'x'; owner.Line.Length = 1;
    {
        uint32_t before = data_signals;
        assert(!x86_ps2_shell_submit() && data_signals == before && submitted == 2);
        assert(kernel_ps2_command_pop(&owner.Commands,&command) == KERNEL_PS2_QUEUE_OK);
    }
#endif
    owner = (X86_PS2_SESSION){0}; actor = 2; feed = 1;
    assert(!x86_ps2_shell_submit());
    x86_ps2_shell_task();
    assert(owner.ShellDone && owner.ShellCommands == 1 && space_signals == 1 && waits == 3);
    puts("PASS: shell bridge full/wait/retry, stop cancellation, separate consumer and EOF; scheduler MOCK");
    return 0;
}
