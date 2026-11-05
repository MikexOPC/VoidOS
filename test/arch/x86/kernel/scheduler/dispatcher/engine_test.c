#include "arch/x86/kernel/scheduler/dispatcher/engine.h"
#include <stdio.h>
#include <stdlib.h>

static uint32_t checks, mode, context = 1, stage[8], collected, cycle;
#define LONG_SESSION_STEPS 70000U // beyond the old 65536 step and 32768 idle caps
static KERNEL_SCHEDULER_REPLY handles[8];
static X86_DISPATCH_CONFIG config;

// ====== check_private ======

static void check_private(int condition)
{
    ++checks;
    if (!condition) { fprintf(stderr,"Engine check %u, mode %u\n",checks,mode); exit(1); }
}

// ====== mocked_cpu_boundary ======

uint32_t kernel_scheduler_context_valid(void) { return context; }
uint32_t kernel_scheduler_context_valid_on(uint32_t cpu) { return cpu == 0 ? context : 0; }
uint32_t kernel_scheduler_binding_context_valid(void) { return context; }
// ====== payload_context_mock ======

uint32_t kernel_scheduler_payload_context_valid(uint32_t consumer, uint64_t generation)
{ (void)consumer; (void)generation; return context; }
__declspec(noreturn) void kernel_scheduler_fail_stop(void) { exit(99); }

// ====== apply_private ======

static KERNEL_SCHEDULER_REPLY apply_private(uint32_t operation, const KERNEL_SCHEDULER_REPLY *task)
{
    return kernel_scheduler_apply(operation,task ? task->index : KERNEL_SCHEDULER_NONE,
        task ? task->generation : 0);
}

// ====== run_private ======

static int run_private(const KERNEL_SCHEDULER_REPLY *task, X86_DISPATCH_COMMAND *command)
{
    uint32_t index = task->index;
    check_private(index < config.task_count && task->generation == handles[index].generation);
    if (mode == 3) { command->reason = 9; return 1; }
    if (mode == 5) { context = 0; return 1; }
    if (!stage[index]) {
        KERNEL_EVENT_REPLY observed = kernel_scheduler_event_apply(0,index,task->generation,(cycle + index) % 8,0);
        check_private(observed.reply.status == 0);
        command->reason = X86_TASK_WAIT; command->target = (cycle + index) % 8; command->token = observed.epoch;
    } else if (mode == 1 && stage[index] == 1) {
        KERNEL_EVENT_REPLY stop = kernel_scheduler_stop_apply(1,index,task->generation);
        KERNEL_EVENT_REPLY observed = kernel_scheduler_event_apply(0,index,task->generation,cycle % 8,0);
        check_private(stop.reply.status == 0 && stop.outcome == 1 && observed.reply.status == 0);
        command->reason = X86_TASK_WAIT; command->target = cycle % 8; command->token = observed.epoch;
    } else if (mode == 9 && stage[index] < LONG_SESSION_STEPS) {
        KERNEL_EVENT_REPLY observed = kernel_scheduler_event_apply(0,index,task->generation,cycle % 8,0);
        check_private(observed.reply.status == 0);
        command->reason = X86_TASK_WAIT; command->target = cycle % 8; command->token = observed.epoch;
    } else { command->reason = X86_TASK_EXIT; }
    ++stage[index];
    return 1;
}

// ====== collect_private ======

static int collect_private(const KERNEL_SCHEDULER_REPLY *task)
{
    check_private(apply_private(KERNEL_SCHEDULER_QUERY,task).state == KERNEL_TASK_FINISHED);
    if (mode == 4) return 0;
    ++collected;
    return 1; // Native resources are MOCK; no heap allocation in this fixture.
}

// ====== reply_private ======

static int reply_private(const KERNEL_SCHEDULER_REPLY *task, const KERNEL_EVENT_REPLY *reply)
{
    check_private(reply->reply.index == task->index && reply->reply.generation == task->generation);
    if (mode == 6) return 0;
    check_private(reply->outcome == KERNEL_EVENT_WAITING ||
        (mode == 1 && reply->outcome == KERNEL_EVENT_STOP_REQUESTED));
    return 1;
}

// ====== release_private ======

static int release_private(void) { return mode != 8; }

// ====== idle_private ======

static int idle_private(void)
{
    X86_DISPATCH_STATS nested = {0};
    check_private(!x86_scheduler_dispatch(&config,&nested));
    check_private(!nested.steps && !nested.failure); // Reentry touches no stats/Owner.
    if (mode == 2) return 1; // Spurious provider success; engine must bound retries.
    if (mode == 1) {
        KERNEL_EVENT_REPLY reply = kernel_scheduler_stop_apply(0,handles[0].index,handles[0].generation);
        check_private(reply.reply.status == 0 && reply.woken == 1);
    } else {
        for (uint32_t index = 0; index < config.task_count; ++index) {
            check_private(kernel_scheduler_irq_post((cycle + index) % 8) == 1);
        }
        {
            KERNEL_EVENT_REPLY reply = kernel_scheduler_irq_drain();
            check_private(reply.reply.status == 0 && reply.woken == config.task_count);
        }
    }
    return 1;
}

// ====== prepare_private ======

static void prepare_private(uint32_t count)
{
    for (uint32_t index = 0; index < count; ++index) {
        handles[index] = apply_private(KERNEL_SCHEDULER_RESERVE,NULL);
        check_private(handles[index].status == 0 && handles[index].index == index);
        check_private(apply_private(KERNEL_SCHEDULER_PUBLISH,&handles[index]).status == 0);
        stage[index] = 0;
    }
    collected = 0;
    config = (X86_DISPATCH_CONFIG){count, mode == 7 ? 1U : 32U,2,1,
        run_private,collect_private,reply_private,release_private,idle_private};
}

// ====== main ======

int main(int argc, char **argv)
{
    X86_DISPATCH_STATS stats = {0};
    check_private(argc == 2);
    mode = (uint32_t)strtoul(argv[1],NULL,10);
    check_private(mode <= 10);
    check_private(apply_private(KERNEL_SCHEDULER_INIT,NULL).status == 0);
    if (mode <= 1) {
        for (cycle = 0; cycle < 1000; ++cycle) {
            uint32_t count = mode == 1 ? 1 : cycle % 3 == 0 ? 1 : cycle % 3 == 1 ? 2 : 8;
            prepare_private(count);
            stats = (X86_DISPATCH_STATS){0};
            check_private(!x86_scheduler_dispatch(NULL,&stats));
            check_private(!x86_scheduler_dispatch(&config,NULL));
            {
                X86_DISPATCH_CONFIG invalid = config;
                invalid.task_count = 9;
                check_private(!x86_scheduler_dispatch(&invalid,&stats));
                invalid = config;
                invalid.idle_limit = 32769;
                check_private(!x86_scheduler_dispatch(&invalid,&stats));
            }
            if (cycle % 2 == 0) config.idle_limit = 32768;
            check_private(x86_scheduler_dispatch(&config,&stats));
            check_private(stats.completed == count && collected == count && stats.failure == 0 && stats.idle_calls == 1);
            check_private(stats.steps == (mode == 1 ? 3U : 2 * count) && stats.waiting_count == count);
            check_private(stats.stop_waits == (mode == 1 ? 1U : 0U));
            check_private(apply_private(KERNEL_SCHEDULER_SELECT,NULL).status == KERNEL_SCHEDULER_EMPTY);
            check_private(!x86_scheduler_dispatch(&config,&stats)); // Stats cannot silently reuse a finished run.
        }
    } else if (mode == 10) {
        prepare_private(2);
        check_private(x86_scheduler_dispatch_retained_on(0,&config,&stats));
        check_private(stats.completed == 2 && collected == 2 && !stats.failure);
        for (uint32_t index = 0; index < 2; ++index)
            check_private(apply_private(KERNEL_SCHEDULER_QUERY,&handles[index]).state == KERNEL_TASK_FINISHED);
        KERNEL_SCHEDULER_REPLY reserved = apply_private(KERNEL_SCHEDULER_RESERVE,NULL);
        check_private(reserved.status == 0 && reserved.index == 2);
        check_private(apply_private(KERNEL_SCHEDULER_ABORT,&reserved).status == 0);
        for (uint32_t index = 0; index < 2; ++index)
            check_private(apply_private(KERNEL_SCHEDULER_COLLECT,&handles[index]).status == 0);
        prepare_private(1);
        stats = (X86_DISPATCH_STATS){0};
        check_private(x86_scheduler_dispatch(&config,&stats) && stats.completed == 1);
    } else if (mode == 9) {
        // B1: an operational session has no fixed length; per-operation timeouts live in idle().
        prepare_private(1);
        config.step_limit = config.idle_limit = X86_DISPATCH_UNLIMITED;
        check_private(x86_scheduler_dispatch(&config,&stats));
        check_private(stats.completed == 1 && stats.failure == 0);
        check_private(stats.steps == LONG_SESSION_STEPS + 1 && stats.idle_calls == LONG_SESSION_STEPS);
    } else {
        uint32_t expected_state;
        prepare_private(1);
        check_private(!x86_scheduler_dispatch(&config,&stats));
        context = 1;
        expected_state = mode == 3 || mode == 5 ? KERNEL_TASK_RUNNING :
            mode == 4 ? KERNEL_TASK_FINISHED : KERNEL_TASK_WAITING;
        check_private(apply_private(KERNEL_SCHEDULER_QUERY,&handles[0]).state == expected_state);
        check_private(!collected && !stats.completed);
        check_private(stats.failure == (mode == 2 ? 2U : mode == 5 ? 4U : mode == 7 ? 3U : mode == 8 ? 6U : 5U));
        if (mode == 2) check_private(stats.idle_calls == 2);
        if (mode == 7) check_private(!stats.idle_calls); // No extra SELECT or hardware wait after budget exhaustion.
    }
    printf("PASS: %u engine checks, mode %u, CPU/resources MOCK, real Rust Owner\n",checks,mode);
    return 0;
}
