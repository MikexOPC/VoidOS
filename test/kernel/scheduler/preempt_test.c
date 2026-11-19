#include <stdio.h>
#include <string.h>
#include "kernel/scheduler/preempt.h"

static KERNEL_PREEMPT_STATE state;
static unsigned int failures;

// ====== check ======

static void check(int condition, const char *name)
{
    if (condition) return;
    ++failures;
    printf("FAIL: %s\n", name);
}

// ====== tick ======

static uint32_t tick(uint64_t task, uint32_t enabled, uint32_t locks, uint32_t line, uint32_t over)
{
    KERNEL_PREEMPT_INPUT input = {task, enabled, locks, line, over};
    return kernel_preempt_decide(&state, &input);
}

// ====== reset ======

static void reset(void)
{
    memset(&state, 0, sizeof(state));
}

// ====== plain_suspend ======

static void plain_suspend(void)
{
    reset();
    check(tick(7, 1, 0, 0, 0) == KERNEL_PREEMPT_IGNORE, "slice running is ignored");
    check(tick(7, 1, 0, 0, 1) == KERNEL_PREEMPT_SUSPEND, "slice over suspends");
    check(state.preempted == 1 && state.ignored == 1 && state.ticks == 2 && !state.overruns,
        "plain suspend counters");
}

// ====== refusals ======

static void refusals(void)
{
    reset();
    check(tick(0, 1, 0, 0, 1) == KERNEL_PREEMPT_IGNORE, "no task is ignored");
    check(tick(7, 0, 0, 0, 1) == KERNEL_PREEMPT_IGNORE, "interrupts disabled is ignored");
    check(tick(7, 1, 1, 0, 1) == KERNEL_PREEMPT_DEFER, "held lock defers");
    check(tick(7, 1, 3, 1, 1) == KERNEL_PREEMPT_DEFER, "held lock defers before the line");
    check(state.line_ticks == 0 && state.deferred_lock == 2 && !state.preempted,
        "lock deferral does not consume the line budget");
    check(kernel_preempt_decide(0, 0) == KERNEL_PREEMPT_IGNORE, "null arguments");
}

// ====== line_boundary ======

static void line_boundary(void)
{
    uint32_t index;
    reset();
    for (index = 0; index < KERNEL_PREEMPT_LINE_TICKS; ++index)
        check(tick(9, 1, 0, 1, 1) == KERNEL_PREEMPT_DEFER, "open line defers within the budget");
    check(state.line_ticks == KERNEL_PREEMPT_LINE_TICKS && state.deferred_line == KERNEL_PREEMPT_LINE_TICKS,
        "line budget used");
    check(tick(9, 1, 0, 1, 1) == KERNEL_PREEMPT_SUSPEND, "budget exhausted suspends");
    check(state.overruns == 1 && state.preempted == 1 && state.line_ticks == 0, "overrun counted once");
    reset();
    check(tick(9, 1, 0, 1, 1) == KERNEL_PREEMPT_DEFER, "first deferral");
    check(tick(9, 1, 0, 0, 1) == KERNEL_PREEMPT_SUSPEND, "closed line suspends at once");
    check(!state.overruns && state.preempted == 1 && state.line_ticks == 0, "closed line is no overrun");
}

// ====== task_change ======

static void task_change(void)
{
    uint32_t index;
    reset();
    for (index = 0; index < KERNEL_PREEMPT_LINE_TICKS; ++index) (void)tick(9, 1, 0, 1, 1);
    check(tick(11, 1, 0, 1, 1) == KERNEL_PREEMPT_DEFER, "new task gets a fresh line budget");
    check(state.task == 11 && state.line_ticks == 1, "task identity tracked");
    (void)tick(11, 1, 0, 1, 0);
    check(state.line_ticks == 0, "running slice clears the line budget");
    (void)tick(11, 1, 0, 1, 1);
    (void)tick(0, 1, 0, 0, 0);
    check(state.task == 0 && state.line_ticks == 0, "idle tick forgets the task");
}

// ====== completed_lines ======

static void completed_lines(void) {
    reset();
    for (uint32_t line = 0; line < 1000; ++line) {
        check(tick(9,1,0,1,1) == KERNEL_PREEMPT_DEFER, "IRQ defers an open line");
        check(kernel_preempt_line_complete(&state,9,1,0) == KERNEL_PREEMPT_SUSPEND,
            "completed line fulfills deferred preemption");
        check(!state.line_ticks && !state.overruns, "line completion clears the pending deferral");
    }
    check(state.preempted == 1000 && state.ticks == 1000, "boundary suspensions preserve IRQ count");
    check(kernel_preempt_line_complete(&state,9,1,0) == KERNEL_PREEMPT_IGNORE, "no pending deferral");
    (void)tick(9,1,0,1,1);
    check(kernel_preempt_line_complete(&state,10,1,0) == KERNEL_PREEMPT_IGNORE, "another task keeps its pending deferral");
    check(kernel_preempt_line_complete(&state,9,0,0) == KERNEL_PREEMPT_IGNORE, "masked caller cannot suspend");
    check(kernel_preempt_line_complete(&state,9,1,1) == KERNEL_PREEMPT_IGNORE, "locked caller cannot suspend");
    check(kernel_preempt_line_complete(&state,9,1,0) == KERNEL_PREEMPT_SUSPEND, "eligible owner consumes pending deferral");
}

// ====== saturation ======

static void saturation(void)
{
    reset();
    state.ticks = state.ignored = state.preempted = UINT64_MAX;
    check(tick(5, 1, 0, 0, 0) == KERNEL_PREEMPT_IGNORE && state.ticks == UINT64_MAX &&
        state.ignored == UINT64_MAX, "counters saturate");
    check(tick(5, 1, 0, 0, 1) == KERNEL_PREEMPT_SUSPEND && state.preempted == UINT64_MAX,
        "suspend still decided at saturation");
}

// ====== main ======

int main(void)
{
    plain_suspend();
    refusals();
    line_boundary();
    task_change();
    completed_lines();
    saturation();
    if (failures) {
        printf("FAIL: preemption policy, %u checks failed\n", failures);
        return 1;
    }
    puts("PASS: preemption policy: suspend, lock and line deferral, 4-tick overrun, task change, saturation");
    return 0;
}
