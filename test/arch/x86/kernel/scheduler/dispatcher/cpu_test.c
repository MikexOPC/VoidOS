#include "arch/x86/kernel/scheduler/dispatcher/engine.h"
#include <stdio.h>
#include <stdlib.h>

static uint32_t checks, cpu, context = 1, locked, acquired, released;
static uint32_t stage, fail_run, replies, collected;
static uint64_t observed;
static KERNEL_SCHEDULER_REPLY handle;
static X86_DISPATCH_CONFIG config;
static uint32_t runtime_completions, runtime_idles;

// ====== check_private ======

static void check_private(int condition)
{
    ++checks;
    if (!condition) { fprintf(stderr,"CPU %u stage %u check %u\n",cpu,stage,checks); exit(1); }
}

// ====== mocked_backend ======

uint32_t kernel_scheduler_context_valid_on(uint32_t requested)
{ return requested == cpu ? context : 0; }
uint32_t kernel_scheduler_context_valid(void) { return cpu == 0 ? context : 0; }
uint32_t kernel_scheduler_binding_context_valid(void) { return kernel_scheduler_context_valid(); }
uint32_t kernel_scheduler_payload_context_valid(uint32_t consumer, uint64_t generation)
{ (void)consumer; (void)generation; return 0; }
uint32_t kernel_scheduler_lock_acquire(void)
{ check_private(!locked); locked = 1; ++acquired; return 1; }
void kernel_scheduler_lock_release(void)
{ check_private(locked == 1); locked = 0; ++released; }
__declspec(noreturn) void kernel_scheduler_fail_stop(void) { exit(99); }

// ====== apply_private ======

static KERNEL_SCHEDULER_REPLY apply_private(uint32_t operation)
{
    return kernel_scheduler_apply_on(cpu,operation,
        operation == KERNEL_SCHEDULER_RESERVE || operation == KERNEL_SCHEDULER_SELECT ?
        KERNEL_SCHEDULER_NONE : handle.index,
        operation == KERNEL_SCHEDULER_RESERVE || operation == KERNEL_SCHEDULER_SELECT ? 0 : handle.generation);
}

// ====== run_private ======

static int run_private(const KERNEL_SCHEDULER_REPLY *task, X86_DISPATCH_COMMAND *command)
{
    X86_DISPATCH_STATS nested = {0};
    check_private(!locked && task->index == handle.index && task->generation == handle.generation);
    check_private(!x86_scheduler_dispatch_on(cpu,&config,&nested));
    check_private(!nested.steps && !nested.failure);
    check_private(!x86_scheduler_dispatch_on((cpu + 1) % 64,&config,&nested));
    check_private(!nested.steps && !nested.failure);
    if (fail_run) { context = fail_run; return 1; }
    command->target = 3;
    switch (stage++) {
    case 0: command->reason = X86_TASK_YIELD; command->target = 0; break;
    case 1: command->reason = X86_TASK_OBSERVE; break;
    case 2: command->reason = X86_TASK_SIGNAL; break;
    case 3: command->reason = X86_TASK_WAIT; command->token = observed; break;
    case 4: command->reason = X86_TASK_STOP_QUERY; command->target = task->index;
        command->token = task->generation; break;
    case 5: command->reason = X86_TASK_STOP; command->target = task->index;
        command->token = task->generation; break;
    case 6: command->reason = X86_TASK_EXIT; command->target = 0; break;
    default: check_private(0);
    }
    return 1;
}

// ====== reply_private ======

static int reply_private(const KERNEL_SCHEDULER_REPLY *task, const KERNEL_EVENT_REPLY *reply)
{
    check_private(!locked && task->index == handle.index && reply->reply.status == KERNEL_SCHEDULER_OK);
    if (stage == 2) observed = reply->epoch;
    if (stage == 4) check_private(reply->outcome == KERNEL_EVENT_CHANGED);
    if (stage == 5) check_private(reply->outcome == 0);
    if (stage == 6) check_private(reply->outcome == 1);
    ++replies;
    return 1;
}

// ====== cleanup_callbacks ======

static int collect_private(const KERNEL_SCHEDULER_REPLY *task)
{ check_private(!locked && task->state == KERNEL_TASK_FINISHED); ++collected; return 1; }
static int release_private(void) { check_private(!locked); return 1; }

// ====== prepare_private ======

static void prepare_private(void)
{
    handle = apply_private(KERNEL_SCHEDULER_RESERVE);
    check_private(handle.status == KERNEL_SCHEDULER_OK);
    check_private(kernel_scheduler_affinity_on(cpu,handle.index,handle.generation,1ULL << cpu) == 0);
    check_private(apply_private(KERNEL_SCHEDULER_PUBLISH).status == KERNEL_SCHEDULER_OK);
    stage = replies = collected = 0;
    config = (X86_DISPATCH_CONFIG){1,16,0,0,run_private,collect_private,reply_private,release_private,0};
}

// ====== runtime_complete_private ======

static int runtime_complete_private(void) {
    ++runtime_completions;
    return 1;
}

// ====== runtime_idle_private ======

static int runtime_idle_private(void) {
    if (++runtime_idles != 1) return 0;
    X86_DISPATCH_CONFIG saved = config;
    prepare_private();
    config = saved;
    return 1;
}

// ====== main ======

int main(void)
{
    check_private(kernel_scheduler_apply(KERNEL_SCHEDULER_INIT,KERNEL_SCHEDULER_NONE,0).status == 0);
    check_private(kernel_scheduler_configure(64,1) == 0);
    for (cpu = 0; cpu < 64; ++cpu) {
        X86_DISPATCH_STATS stats = {0};
        prepare_private();
        check_private(!x86_scheduler_dispatch_on(64,&config,&stats));
        check_private(!x86_scheduler_dispatch_on(UINT32_MAX,&config,&stats));
        context = 2;
        check_private(!x86_scheduler_dispatch_on(cpu,&config,&stats));
        context = 0;
        check_private(!x86_scheduler_dispatch_on(cpu,&config,&stats));
        context = 1;
        if (cpu) {
            check_private(!x86_scheduler_dispatch(&config,&stats));
            config.drain_irq = 1;
            check_private(!x86_scheduler_dispatch_on(cpu,&config,&stats));
            config.drain_irq = 0;
        }
        check_private(!stats.steps && !stats.failure && !locked);
        check_private(x86_scheduler_dispatch_retained_on(cpu,&config,&stats));
        check_private(apply_private(KERNEL_SCHEDULER_QUERY).state == KERNEL_TASK_FINISHED);
        check_private(apply_private(KERNEL_SCHEDULER_COLLECT).status == 0);
        check_private(stats.steps == 7 && stats.completed == 1 && !stats.failure &&
            stats.changed_count == 1 && stats.signal_count == 1 && !stats.waiting_count &&
            replies == 5 && collected == 1);
        for (fail_run = 2; fail_run <= 3; ++fail_run) {
            prepare_private();
            stats = (X86_DISPATCH_STATS){0};
            check_private(!x86_scheduler_dispatch_on(cpu,&config,&stats));
            check_private(stats.failure == 4 && stats.steps == 1 && !locked);
            context = 1;
            check_private(apply_private(KERNEL_SCHEDULER_EXIT).status == 0);
            check_private(apply_private(KERNEL_SCHEDULER_COLLECT).status == 0);
        }
        fail_run = 0;
        prepare_private();
        stats = (X86_DISPATCH_STATS){0};
        check_private(x86_scheduler_dispatch_on(cpu,&config,&stats));
        check_private(!stats.failure && stats.completed == 1);
        if (cpu) {
            runtime_completions = runtime_idles = 0;
            config.task_count = 0;
            config.step_limit = X86_DISPATCH_UNLIMITED;
            config.idle_limit = X86_DISPATCH_UNLIMITED;
            config.idle = runtime_idle_private;
            stats = (X86_DISPATCH_STATS){0};
            check_private(!x86_scheduler_dispatch_continuous_on(cpu,&config,&stats,
                runtime_complete_private,runtime_idle_private));
            check_private(runtime_completions == 1 && runtime_idles == 2 &&
                stats.completed == 1 && stats.steps == 7 && stats.failure == 2);
            check_private(apply_private(KERNEL_SCHEDULER_QUERY).state == KERNEL_TASK_FINISHED);
            check_private(apply_private(KERNEL_SCHEDULER_COLLECT).status == 0);
        }
    }
    check_private(!locked && acquired == released);
    printf("PASS: %u dispatcher checks, 64 sequential CPU identities, real locked Rust Owner; CPU/stacks MOCK\n",checks);
    return 0;
}
