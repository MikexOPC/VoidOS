#include "kernel/scheduler/scheduler.h"
#include <stdio.h>
#include <stdlib.h>

static uint32_t context = 1;
static uint32_t reenter;
static uint32_t callbacks;
static uint32_t checks;

// ====== binding_context_mock ======

uint32_t kernel_scheduler_binding_context_valid(void) { return context; }
// ====== payload_context_mock ======

uint32_t kernel_scheduler_payload_context_valid(uint32_t consumer, uint64_t generation)
{ (void)consumer; (void)generation; return context; }

// ====== check ======

static void check(int condition)
{
    if (!condition) { fprintf(stderr, "FFI check failed: %u\n", checks + 1); exit(1); }
    ++checks;
}

// ====== kernel_scheduler_context_valid ======

uint32_t kernel_scheduler_context_valid(void)
{
    ++callbacks;
    if (reenter) {
        KERNEL_SCHEDULER_REPLY nested;
        uint32_t previous = callbacks;
        nested = kernel_scheduler_apply(KERNEL_SCHEDULER_INIT, KERNEL_SCHEDULER_NONE, 0);
        check(nested.status == KERNEL_SCHEDULER_BUSY);
        check(nested.index == KERNEL_SCHEDULER_NONE && nested.generation == 0);
        check(callbacks == previous);
        {
            KERNEL_EVENT_REPLY blocked = kernel_scheduler_event_apply(0, 0, 1, 0, 0);
            check(blocked.reply.status == KERNEL_SCHEDULER_BUSY && blocked.outcome == UINT32_MAX);
            check(blocked.epoch == 0 && blocked.woken == 0 && callbacks == previous);
            blocked = kernel_scheduler_irq_drain();
            check(blocked.reply.status == KERNEL_SCHEDULER_BUSY && callbacks == previous);
            check(kernel_scheduler_irq_post(7) == 1 && callbacks == previous);
            blocked = kernel_scheduler_stop_apply(KERNEL_STOP_REQUEST,0,1);
            check(blocked.reply.status == KERNEL_SCHEDULER_BUSY && callbacks == previous);
        }
    }
    return context;
}

// ====== kernel_scheduler_fail_stop ======

__declspec(noreturn) void kernel_scheduler_fail_stop(void)
{
    fputs("Unexpected Rust terminal backend\n", stderr);
    exit(98);
}

// ====== no_handle ======

static KERNEL_SCHEDULER_REPLY no_handle(uint32_t operation)
{
    return kernel_scheduler_apply(operation, KERNEL_SCHEDULER_NONE, 0);
}

// ====== with_handle ======

static KERNEL_SCHEDULER_REPLY with_handle(uint32_t operation, KERNEL_SCHEDULER_REPLY handle)
{
    return kernel_scheduler_apply(operation, handle.index, handle.generation);
}

// ====== lifecycle_stress ======

static void lifecycle_stress(void)
{
    KERNEL_SCHEDULER_REPLY handles[8];
    uint64_t previous_generation[8] = {0};
    uint32_t repetition, index, round;
    for (repetition = 0; repetition < 100; ++repetition) {
        for (index = 0; index < 8; ++index) {
            KERNEL_SCHEDULER_REPLY result;
            handles[index] = no_handle(KERNEL_SCHEDULER_RESERVE);
            check(handles[index].status == KERNEL_SCHEDULER_OK);
            check(handles[index].index == index);
            check(handles[index].generation > previous_generation[index]);
            result = with_handle(KERNEL_SCHEDULER_PUBLISH, handles[index]);
            check(result.state == KERNEL_TASK_READY && result.ready == index + 1);
        }
        check(no_handle(KERNEL_SCHEDULER_RESERVE).status == KERNEL_SCHEDULER_FULL);
        for (round = 0; round < 100; ++round) {
            for (index = 0; index < 8; ++index) {
                KERNEL_SCHEDULER_REPLY selected = no_handle(KERNEL_SCHEDULER_SELECT);
                KERNEL_SCHEDULER_REPLY result;
                check(selected.status == 0 && selected.index == handles[index].index);
                check(selected.generation == handles[index].generation);
                check(selected.state == KERNEL_TASK_RUNNING && selected.ready == 7);
                result = no_handle(KERNEL_SCHEDULER_CURRENT);
                check(result.index == index && result.generation == handles[index].generation);
                check(with_handle(KERNEL_SCHEDULER_COLLECT, selected).status == KERNEL_SCHEDULER_RUNNING);
                result = with_handle(KERNEL_SCHEDULER_YIELD, selected);
                check(result.status == 0 && result.state == KERNEL_TASK_READY && result.ready == 8);
            }
        }
        for (index = 0; index < 8; ++index) {
            KERNEL_SCHEDULER_REPLY selected = no_handle(KERNEL_SCHEDULER_SELECT);
            KERNEL_SCHEDULER_REPLY result;
            check(selected.index == index);
            result = with_handle(KERNEL_SCHEDULER_EXIT, selected);
            check(result.status == 0 && result.state == KERNEL_TASK_FINISHED);
            result = with_handle(KERNEL_SCHEDULER_COLLECT, selected);
            check(result.status == 0 && result.state == KERNEL_TASK_FREE);
            check(with_handle(KERNEL_SCHEDULER_COLLECT, selected).status == KERNEL_SCHEDULER_BAD_STATE);
            previous_generation[index] = handles[index].generation;
        }
        check(no_handle(KERNEL_SCHEDULER_SELECT).status == KERNEL_SCHEDULER_EMPTY);
    }
}

// ====== event_lifecycle ======

static void event_lifecycle(void)
{
    KERNEL_SCHEDULER_REPLY a = no_handle(KERNEL_SCHEDULER_RESERVE);
    KERNEL_SCHEDULER_REPLY b = no_handle(KERNEL_SCHEDULER_RESERVE);
    KERNEL_EVENT_REPLY result;
    check(with_handle(KERNEL_SCHEDULER_PUBLISH, a).status == 0);
    check(with_handle(KERNEL_SCHEDULER_PUBLISH, b).status == 0);
    check(no_handle(KERNEL_SCHEDULER_SELECT).index == a.index);
    result = kernel_scheduler_event_apply(0, a.index, a.generation, 0, 0);
    check(result.reply.status == 0 && result.epoch == 0 && result.outcome == 0);
    check(kernel_scheduler_event_apply(1, a.index, a.generation, 0, 1).reply.status == KERNEL_SCHEDULER_BAD_TOKEN);
    check(kernel_scheduler_event_apply(2, a.index, a.generation, 8, 0).reply.status == KERNEL_SCHEDULER_BAD_EVENT);
    check(kernel_scheduler_event_apply(2, a.index, a.generation, 0, 1).reply.status == KERNEL_SCHEDULER_BAD_ARGUMENT);
    check(kernel_scheduler_event_apply(3, a.index, a.generation, 0, 0).reply.status == KERNEL_SCHEDULER_BAD_ARGUMENT);
    context = 0;
    result = kernel_scheduler_event_apply(0, a.index, a.generation, 0, 0);
    check(result.reply.status == KERNEL_SCHEDULER_DENIED && result.epoch == 0 && result.outcome == UINT32_MAX);
    context = 1;
    result = kernel_scheduler_event_apply(1, a.index, a.generation, 0, 0);
    check(result.reply.status == 0 && result.reply.state == KERNEL_TASK_WAITING && result.outcome == 1);
    check(no_handle(KERNEL_SCHEDULER_SELECT).index == b.index);
    result = kernel_scheduler_event_apply(2, b.index, b.generation, 0, 0);
    check(result.reply.status == 0 && result.epoch == 1 && result.woken == 1 && result.reply.ready == 1);
    check(with_handle(KERNEL_SCHEDULER_EXIT, b).status == 0);
    check(with_handle(KERNEL_SCHEDULER_COLLECT, b).status == 0);
    check(no_handle(KERNEL_SCHEDULER_SELECT).index == a.index);
    result = kernel_scheduler_event_apply(1, a.index, a.generation, 0, 0);
    check(result.reply.status == 0 && result.outcome == 2 && result.reply.state == KERNEL_TASK_READY);
    check(no_handle(KERNEL_SCHEDULER_SELECT).index == a.index);
    check(with_handle(KERNEL_SCHEDULER_EXIT, a).status == 0);
    check(with_handle(KERNEL_SCHEDULER_COLLECT, a).status == 0);
    check(kernel_scheduler_event_apply(0, a.index, a.generation + 1, 0, 0).reply.status == KERNEL_SCHEDULER_BAD_HANDLE);
}

// ====== irq_lifecycle ======

static void irq_lifecycle(void)
{
    KERNEL_SCHEDULER_REPLY a = no_handle(KERNEL_SCHEDULER_RESERVE);
    KERNEL_EVENT_REPLY result;
    check(kernel_scheduler_irq_post(8) == 0);
    check(kernel_scheduler_irq_post(UINT32_MAX) == 0);
    check(kernel_scheduler_irq_drain().reply.status == 0); // Reentrant callback posted event 7.
    check(with_handle(KERNEL_SCHEDULER_PUBLISH, a).status == 0);
    check(no_handle(KERNEL_SCHEDULER_SELECT).index == a.index);
    check(kernel_scheduler_event_apply(1, a.index, a.generation, 3, 0).outcome == 1);
    check(no_handle(KERNEL_SCHEDULER_SELECT).status == KERNEL_SCHEDULER_BLOCKED);
    for (uint32_t index = 0; index < 10000; ++index) check(kernel_scheduler_irq_post(3) == 1);
    context = 0;
    check(kernel_scheduler_irq_drain().reply.status == KERNEL_SCHEDULER_DENIED);
    context = 1;
    result = kernel_scheduler_irq_drain();
    check(result.reply.status == 0 && result.woken == 1 && result.reply.ready == 1);
    check(kernel_scheduler_irq_drain().woken == 0);
    check(no_handle(KERNEL_SCHEDULER_SELECT).index == a.index);
    result = kernel_scheduler_event_apply(0, a.index, a.generation, 3, 0);
    check(result.epoch == 1); // 10000 posts coalesced, cardinality lives in the driver.
    check(kernel_scheduler_irq_post(3) == 1);
    check(kernel_scheduler_irq_drain().reply.status == KERNEL_SCHEDULER_RUNNING);
    check(with_handle(KERNEL_SCHEDULER_YIELD, a).status == 0);
    check(kernel_scheduler_irq_drain().reply.status == 0); // Rejected drain preserved bit.
    check(no_handle(KERNEL_SCHEDULER_SELECT).index == a.index);
    check(kernel_scheduler_event_apply(0, a.index, a.generation, 3, 0).epoch == 2);
    check(with_handle(KERNEL_SCHEDULER_EXIT, a).status == 0);
    check(with_handle(KERNEL_SCHEDULER_COLLECT, a).status == 0);
}

// ====== blocked_lifecycle ======

static void idle_cycles(void)
{
    for (uint32_t iteration = 0; iteration < 1000; ++iteration) {
        KERNEL_SCHEDULER_REPLY tasks[2];
        uint64_t epoch = 0;
        for (uint32_t index = 0; index < 2; ++index) {
            tasks[index] = no_handle(KERNEL_SCHEDULER_RESERVE);
            check(tasks[index].status == 0);
            check(with_handle(KERNEL_SCHEDULER_PUBLISH,tasks[index]).status == 0);
        }
        for (uint32_t index = 0; index < 2; ++index) {
            KERNEL_EVENT_REPLY event;
            check(no_handle(KERNEL_SCHEDULER_SELECT).index == tasks[index].index);
            event = kernel_scheduler_event_apply(0,tasks[index].index,tasks[index].generation,4,0);
            check(event.reply.status == 0 && event.epoch == iteration);
            epoch = event.epoch;
            event = kernel_scheduler_event_apply(1,tasks[index].index,tasks[index].generation,4,epoch);
            check(event.reply.status == 0 && event.reply.state == KERNEL_TASK_WAITING);
        }
        check(no_handle(KERNEL_SCHEDULER_SELECT).status == KERNEL_SCHEDULER_BLOCKED);
        check(kernel_scheduler_irq_drain().woken == 0);
        check(kernel_scheduler_irq_post(4) == 1);
        check(kernel_scheduler_irq_post(4) == 1);
        {
            KERNEL_EVENT_REPLY wake = kernel_scheduler_irq_drain();
            check(wake.reply.status == 0 && wake.woken == 2 && wake.reply.ready == 2);
        }
        for (uint32_t index = 0; index < 2; ++index) {
            check(no_handle(KERNEL_SCHEDULER_SELECT).index == tasks[index].index);
            check(kernel_scheduler_event_apply(0,tasks[index].index,tasks[index].generation,4,0).epoch == epoch + 1);
            check(with_handle(KERNEL_SCHEDULER_EXIT,tasks[index]).status == 0);
            check(with_handle(KERNEL_SCHEDULER_COLLECT,tasks[index]).status == 0);
        }
        check(no_handle(KERNEL_SCHEDULER_SELECT).status == KERNEL_SCHEDULER_EMPTY);
    }
}

// ====== blocked_lifecycle ======

static void blocked_lifecycle(void)
{
    KERNEL_SCHEDULER_REPLY a = no_handle(KERNEL_SCHEDULER_RESERVE);
    KERNEL_SCHEDULER_REPLY b = no_handle(KERNEL_SCHEDULER_RESERVE);
    check(with_handle(KERNEL_SCHEDULER_PUBLISH, a).status == 0);
    check(with_handle(KERNEL_SCHEDULER_PUBLISH, b).status == 0);
    check(no_handle(KERNEL_SCHEDULER_SELECT).index == a.index);
    check(kernel_scheduler_event_apply(1, a.index, a.generation, 2, 0).reply.state == KERNEL_TASK_WAITING);
    check(no_handle(KERNEL_SCHEDULER_SELECT).index == b.index);
    check(kernel_scheduler_event_apply(1, b.index, b.generation, 2, 0).reply.state == KERNEL_TASK_WAITING);
    check(no_handle(KERNEL_SCHEDULER_SELECT).status == KERNEL_SCHEDULER_BLOCKED);
    check(with_handle(KERNEL_SCHEDULER_COLLECT, a).status == KERNEL_SCHEDULER_BAD_STATE);
    check(with_handle(KERNEL_SCHEDULER_QUERY, a).state == KERNEL_TASK_WAITING);
    check(with_handle(KERNEL_SCHEDULER_QUERY, b).state == KERNEL_TASK_WAITING);
}

// ====== stop_lifecycle ======

static void stop_lifecycle(void)
{
    for (uint32_t cycle = 0; cycle < 1000; ++cycle) {
        KERNEL_SCHEDULER_REPLY task = no_handle(KERNEL_SCHEDULER_RESERVE);
        KERNEL_EVENT_REPLY output;
        check(task.status == 0);
        check(kernel_scheduler_stop_apply(0,task.index,task.generation).reply.status == KERNEL_SCHEDULER_BAD_STATE);
        check(with_handle(KERNEL_SCHEDULER_PUBLISH,task).status == 0);
        check(kernel_scheduler_stop_apply(1,task.index,task.generation).outcome == 0);
        check(no_handle(KERNEL_SCHEDULER_SELECT).index == task.index);
        check(kernel_scheduler_stop_apply(0,task.index,task.generation).reply.status == KERNEL_SCHEDULER_RUNNING);
        output = kernel_scheduler_event_apply(0,task.index,task.generation,5,0);
        check(output.reply.status == 0);
        check(kernel_scheduler_event_apply(1,task.index,task.generation,5,output.epoch).outcome == 1);
        context = 0;
        output = kernel_scheduler_stop_apply(0,task.index,task.generation);
        check(output.reply.status == KERNEL_SCHEDULER_DENIED && output.outcome == UINT32_MAX && output.reply.ready == 0);
        context = 1;
        check(kernel_scheduler_stop_apply(2,task.index,task.generation).reply.status == KERNEL_SCHEDULER_BAD_ARGUMENT);
        check(kernel_scheduler_stop_apply(0,task.index,task.generation + 1).reply.status == KERNEL_SCHEDULER_BAD_HANDLE);
        output = kernel_scheduler_stop_apply(0,task.index,task.generation);
        check(output.reply.status == 0 && output.outcome == 1 && output.woken == 1 && output.epoch == 0);
        check(output.reply.state == KERNEL_TASK_READY && output.reply.ready == 1);
        {
            KERNEL_EVENT_REPLY duplicate = kernel_scheduler_stop_apply(0,task.index,task.generation);
            check(duplicate.reply.status == 0 && duplicate.woken == 0 && duplicate.reply.events == output.reply.events);
        }
        check(with_handle(KERNEL_SCHEDULER_COLLECT,task).status == KERNEL_SCHEDULER_BAD_STATE);
        check(no_handle(KERNEL_SCHEDULER_SELECT).index == task.index);
        check(kernel_scheduler_stop_apply(1,task.index,task.generation).outcome == 1);
        check(kernel_scheduler_event_apply(1,task.index,task.generation,5,output.epoch + 1).reply.status == KERNEL_SCHEDULER_BAD_TOKEN);
        output = kernel_scheduler_event_apply(0,task.index,task.generation,5,0);
        check(output.reply.status == 0);
        check(kernel_scheduler_event_apply(1,task.index,task.generation,5,output.epoch).outcome == KERNEL_EVENT_STOP_REQUESTED);
        check(no_handle(KERNEL_SCHEDULER_SELECT).index == task.index);
        check(with_handle(KERNEL_SCHEDULER_EXIT,task).status == 0);
        check(with_handle(KERNEL_SCHEDULER_COLLECT,task).status == 0);
        check(kernel_scheduler_stop_apply(1,task.index,task.generation).reply.status == KERNEL_SCHEDULER_BAD_STATE);
        check(no_handle(KERNEL_SCHEDULER_SELECT).status == KERNEL_SCHEDULER_EMPTY);
    }
}

// ====== main ======

int main(void)
{
    KERNEL_SCHEDULER_REPLY handle, result;
    uint32_t invalid;
    context = 0;
    check(no_handle(KERNEL_SCHEDULER_INIT).status == KERNEL_SCHEDULER_DENIED);
    context = 2;
    check(no_handle(KERNEL_SCHEDULER_INIT).status == KERNEL_SCHEDULER_DENIED);
    context = 1;
    check(no_handle(KERNEL_SCHEDULER_SELECT).status == KERNEL_SCHEDULER_NOT_INITIALIZED);
    check(kernel_scheduler_event_apply(0, 0, 1, 0, 0).reply.status == KERNEL_SCHEDULER_NOT_INITIALIZED);
    reenter = 1;
    check(no_handle(KERNEL_SCHEDULER_INIT).status == KERNEL_SCHEDULER_OK);
    reenter = 0;
    check(no_handle(KERNEL_SCHEDULER_INIT).status == KERNEL_SCHEDULER_ALREADY_INITIALIZED);
    check(kernel_scheduler_apply(KERNEL_SCHEDULER_RESERVE, 0, 0).status == KERNEL_SCHEDULER_BAD_ARGUMENT);
    check(kernel_scheduler_apply(KERNEL_SCHEDULER_INIT, KERNEL_SCHEDULER_NONE, 1).status == KERNEL_SCHEDULER_BAD_ARGUMENT);
    for (invalid = 10; invalid < 32; ++invalid) {
        check(no_handle(invalid).status == KERNEL_SCHEDULER_BAD_ARGUMENT);
    }
    check(no_handle(UINT32_MAX).status == KERNEL_SCHEDULER_BAD_ARGUMENT);
    handle = no_handle(KERNEL_SCHEDULER_RESERVE);
    check(with_handle(KERNEL_SCHEDULER_PUBLISH, handle).status == KERNEL_SCHEDULER_OK);
    result = kernel_scheduler_apply(KERNEL_SCHEDULER_QUERY, handle.index,
                                    handle.generation | (UINT64_C(1) << 32));
    check(result.status == KERNEL_SCHEDULER_BAD_HANDLE);
    check(result.generation == 0 && result.index == KERNEL_SCHEDULER_NONE && result.ready == 1);
    check(kernel_scheduler_apply(KERNEL_SCHEDULER_QUERY, UINT32_MAX,
                                handle.generation).status == KERNEL_SCHEDULER_BAD_HANDLE);
    context = 0;
    check(no_handle(KERNEL_SCHEDULER_SELECT).status == KERNEL_SCHEDULER_DENIED);
    context = 1;
    result = no_handle(KERNEL_SCHEDULER_SELECT);
    check(result.index == handle.index && result.generation == handle.generation);
    check(kernel_scheduler_apply(KERNEL_SCHEDULER_EXIT, handle.index, 0).status == KERNEL_SCHEDULER_BAD_HANDLE);
    check(with_handle(KERNEL_SCHEDULER_EXIT, handle).status == KERNEL_SCHEDULER_OK);
    check(with_handle(KERNEL_SCHEDULER_COLLECT, handle).status == KERNEL_SCHEDULER_OK);
    handle = no_handle(KERNEL_SCHEDULER_RESERVE);
    check(with_handle(KERNEL_SCHEDULER_ABORT, handle).status == KERNEL_SCHEDULER_OK);
    check(with_handle(KERNEL_SCHEDULER_ABORT, handle).status == KERNEL_SCHEDULER_BAD_STATE);
    lifecycle_stress();
    event_lifecycle();
    irq_lifecycle();
    idle_cycles();
    stop_lifecycle();
    blocked_lifecycle();
    printf("PASS: %u C/Rust ABI checks, 80000 select/yield pairs, context predicate MOCK\n", checks);
    return 0;
}
