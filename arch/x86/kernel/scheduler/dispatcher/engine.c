#include "engine.h"

static uint32_t entered; // BSP/IF=0 entry; no SMP synchronization claim.

// ====== dispatch_apply_private ======

static int dispatch_apply_private(uint32_t operation, const KERNEL_SCHEDULER_REPLY *task,
    KERNEL_SCHEDULER_REPLY *reply)
{
    *reply = kernel_scheduler_apply(operation,task->index,task->generation);
    return reply->status == KERNEL_SCHEDULER_OK && reply->index == task->index &&
        reply->generation == task->generation;
}

// ====== dispatch_command_private ======

static int dispatch_command_private(const X86_DISPATCH_CONFIG *config, X86_DISPATCH_STATS *stats,
    const KERNEL_SCHEDULER_REPLY *task, const X86_DISPATCH_COMMAND *command)
{
    KERNEL_SCHEDULER_REPLY reply;
    KERNEL_EVENT_REPLY result;
    if (command->reason < X86_TASK_YIELD || command->reason > X86_TASK_STOP_QUERY) return 0;
    if (command->reason >= X86_TASK_STOP) {
        if (!dispatch_apply_private(KERNEL_SCHEDULER_YIELD,task,&reply)) return 0;
        result = kernel_scheduler_stop_apply(command->reason - X86_TASK_STOP,command->target,command->token);
        if (result.reply.status != KERNEL_SCHEDULER_OK) return 0;
        return config->reply(task,&result);
    }
    if (command->reason >= X86_TASK_OBSERVE) {
        result = kernel_scheduler_event_apply(command->reason - X86_TASK_OBSERVE,task->index,
            task->generation,command->target,command->token);
        if (result.reply.status != KERNEL_SCHEDULER_OK || !config->reply(task,&result)) return 0;
        if (command->reason == X86_TASK_WAIT) {
            if (result.outcome == KERNEL_EVENT_WAITING) ++stats->waiting_count;
            else if (result.outcome == KERNEL_EVENT_CHANGED) ++stats->changed_count;
            else if (result.outcome == KERNEL_EVENT_STOP_REQUESTED) ++stats->stop_waits;
            else return 0;
            return 1;
        }
        if (command->reason == X86_TASK_SIGNAL) ++stats->signal_count;
        return dispatch_apply_private(KERNEL_SCHEDULER_YIELD,task,&reply);
    }
    if (command->target || command->token) return 0;
    if (!dispatch_apply_private(command->reason == X86_TASK_EXIT ? KERNEL_SCHEDULER_EXIT :
        KERNEL_SCHEDULER_YIELD,task,&reply)) return 0;
    if (command->reason == X86_TASK_EXIT) {
        if (reply.state != KERNEL_TASK_FINISHED || !config->collect(&reply) ||
            !dispatch_apply_private(KERNEL_SCHEDULER_COLLECT,task,&reply)) return 0;
        ++stats->completed;
    }
    return 1;
}

// ====== x86_scheduler_dispatch ======

int x86_scheduler_dispatch(const X86_DISPATCH_CONFIG *config, X86_DISPATCH_STATS *stats)
{
    if (entered || !config || !stats || !config->run || !config->collect || !config->reply || !config->release_actor ||
        !config->task_count || config->task_count > KERNEL_SCHEDULER_CAPACITY ||
        config->step_limit < config->task_count || config->step_limit > 65536 ||
        config->idle_limit > 32768 || config->drain_irq > 1 || (!config->idle && config->idle_limit) ||
        stats->steps || stats->completed || stats->waiting_count || stats->changed_count ||
        stats->signal_count || stats->stop_waits || stats->idle_calls || stats->failure) return 0;
    entered = 1;
    while (stats->completed < config->task_count) {
        KERNEL_SCHEDULER_REPLY task;
        X86_DISPATCH_COMMAND command = {0};
        stats->failure = 1;
        if (!kernel_scheduler_context_valid()) goto failure;
        if (stats->steps >= config->step_limit) { stats->failure = 3; goto failure; }
        task = kernel_scheduler_apply(KERNEL_SCHEDULER_SELECT,KERNEL_SCHEDULER_NONE,0);
        if (task.status == KERNEL_SCHEDULER_BLOCKED) {
            stats->failure = 2;
            if (!config->idle || stats->idle_calls >= config->idle_limit) goto failure;
            ++stats->idle_calls;
            if (!config->idle() || !kernel_scheduler_context_valid()) goto failure;
            continue; // SELECT again; a provider return is not proof of a READY task.
        }
        stats->failure = 3;
        if (task.status != KERNEL_SCHEDULER_OK || task.index >= KERNEL_SCHEDULER_CAPACITY ||
            !task.generation || task.state != KERNEL_TASK_RUNNING) goto failure;
        ++stats->steps;
        stats->failure = 4;
        if (!config->run(&task,&command) || !kernel_scheduler_context_valid()) goto failure;
        stats->failure = 5;
        if (!dispatch_command_private(config,stats,&task,&command)) goto failure;
        stats->failure = 6;
        if (!config->release_actor() || !kernel_scheduler_context_valid()) goto failure;
        if (config->drain_irq && kernel_scheduler_irq_drain().reply.status != KERNEL_SCHEDULER_OK) goto failure;
    }
    stats->failure = 7;
    if (kernel_scheduler_apply(KERNEL_SCHEDULER_SELECT,KERNEL_SCHEDULER_NONE,0).status != KERNEL_SCHEDULER_EMPTY)
        goto failure;
    stats->failure = 0;
    entered = 0;
    return 1;
failure:
    entered = 0;
    return 0;
}
