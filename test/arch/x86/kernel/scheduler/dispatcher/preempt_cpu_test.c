#include "arch/x86/kernel/scheduler/dispatcher/engine.h"
#include <stdio.h>
#include <stdlib.h>

static uint32_t cpu, checks, held, acquired, released, stages[2], collected;
static KERNEL_SCHEDULER_REPLY tasks[2];

// ====== check_private ======

static void check_private(int value)
{ ++checks; if (!value) { fprintf(stderr,"Preempt CPU %u check %u\n",cpu,checks); exit(1); } }

// ====== mocked_native_authority ======

uint32_t kernel_scheduler_context_valid_on(uint32_t actor) { return actor == cpu; }
uint32_t kernel_scheduler_context_valid(void) { return cpu == 0; }
uint32_t kernel_scheduler_binding_context_valid(void) { return cpu == 0; }
uint32_t kernel_scheduler_payload_context_valid(uint32_t consumer, uint64_t generation)
{ (void)consumer; (void)generation; return 0; }
uint32_t kernel_scheduler_lock_acquire(void)
{ check_private(!held); held = 1; ++acquired; return 1; }
void kernel_scheduler_lock_release(void)
{ check_private(held); held = 0; ++released; }
__declspec(noreturn) void kernel_scheduler_fail_stop(void) { exit(99); }

// ====== callbacks_private ======

static int run_private(const KERNEL_SCHEDULER_REPLY *task, X86_DISPATCH_COMMAND *command)
{
    check_private(!held && task->index < 2 && task->generation == tasks[task->index].generation);
    command->reason = stages[task->index]++ ? X86_TASK_EXIT : X86_TASK_PREEMPT;
    return 1;
}
static int collect_private(const KERNEL_SCHEDULER_REPLY *task)
{ check_private(!held && task->state == KERNEL_TASK_FINISHED); ++collected; return 1; }
static int reply_private(const KERNEL_SCHEDULER_REPLY *task, const KERNEL_EVENT_REPLY *reply)
{ (void)task; (void)reply; check_private(0); return 0; }
static int release_private(void) { check_private(!held); return 1; }

// ====== main ======

int main(void)
{
    check_private(kernel_scheduler_apply(KERNEL_SCHEDULER_INIT,KERNEL_SCHEDULER_NONE,0).status == 0);
    check_private(kernel_scheduler_configure(64,1) == 0);
    for (cpu = 0; cpu < 64; ++cpu) {
        X86_DISPATCH_STATS stats = {0};
        X86_DISPATCH_CONFIG config = {2,8,0,0,run_private,collect_private,reply_private,release_private,NULL};
        collected = 0;
        for (uint32_t index = 0; index < 2; ++index) {
            stages[index] = 0;
            tasks[index] = kernel_scheduler_apply_on(cpu,KERNEL_SCHEDULER_RESERVE,KERNEL_SCHEDULER_NONE,0);
            check_private(tasks[index].status == 0 && tasks[index].index == index);
            check_private(kernel_scheduler_affinity_on(cpu,index,tasks[index].generation,1ULL << cpu) == 0);
            check_private(kernel_scheduler_apply_on(cpu,KERNEL_SCHEDULER_PUBLISH,index,tasks[index].generation).status == 0);
        }
        check_private(x86_scheduler_dispatch_retained_on(cpu,&config,&stats));
        check_private(!stats.failure && stats.steps == 4 && stats.completed == 2 && collected == 2);
        for (uint32_t index = 0; index < 2; ++index) {
            check_private(stages[index] == 2);
            check_private(kernel_scheduler_apply_on(cpu,KERNEL_SCHEDULER_QUERY,index,tasks[index].generation).state == KERNEL_TASK_FINISHED);
            check_private(kernel_scheduler_apply_on(cpu,KERNEL_SCHEDULER_COLLECT,index,tasks[index].generation).status == 0);
        }
    }
    check_private(!held && acquired == released);
    printf("AP_PREEMPT_ENGINE_PASS: %u checks, 64 sequential identities; real Rust tick/preempt, CPU/stacks MOCK\n",checks);
    return 0;
}
