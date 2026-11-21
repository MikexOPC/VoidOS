#include "engine.h"
#include "kernel/scheduler/lot.h"

static unsigned char entered[64];

// ====== dispatch_apply_private ======

static int dispatch_apply_private(uint32_t cpu, uint32_t operation, const KERNEL_SCHEDULER_REPLY *task,
    KERNEL_SCHEDULER_REPLY *reply)
{
    *reply = kernel_scheduler_apply_on(cpu,operation,task->index,task->generation);
    return reply->status == KERNEL_SCHEDULER_OK && reply->index == task->index &&
        reply->generation == task->generation;
}

// ====== dispatch_command_private ======

static int dispatch_command_private(uint32_t cpu, uint32_t retained, const X86_DISPATCH_CONFIG *config, X86_DISPATCH_STATS *stats,
    const KERNEL_SCHEDULER_REPLY *task, const X86_DISPATCH_COMMAND *command)
{
    KERNEL_SCHEDULER_REPLY reply;
    KERNEL_EVENT_REPLY result;
    if (command->reason == X86_TASK_TICK) {
        if (command->target || command->token) return 0;
        reply = kernel_scheduler_tick_on(cpu);
        if (reply.status != KERNEL_SCHEDULER_OK || reply.state > 1) return 0;
        if (!reply.state) return dispatch_apply_private(cpu,KERNEL_SCHEDULER_YIELD,task,&reply);
        reply = kernel_scheduler_apply_on(cpu,KERNEL_SCHEDULER_PREEMPT,task->index,task->generation);
        return reply.status == KERNEL_SCHEDULER_OK && reply.state == KERNEL_TASK_READY &&
            reply.index == task->index && reply.generation == task->generation;
    }
    if (command->reason == X86_TASK_PREEMPT) {
        if (command->target || command->token) return 0;
        reply = kernel_scheduler_tick_on(cpu);
        if (reply.status != KERNEL_SCHEDULER_OK || reply.state != 1) return 0;
        reply = kernel_scheduler_apply_on(cpu,KERNEL_SCHEDULER_PREEMPT,task->index,task->generation);
        return reply.status == KERNEL_SCHEDULER_OK && reply.state == KERNEL_TASK_READY &&
            reply.index == task->index && reply.generation == task->generation;
    }
    if (command->reason < X86_TASK_YIELD || command->reason > X86_TASK_STOP_QUERY) return 0;
    if (command->reason >= X86_TASK_STOP) {
        if (!dispatch_apply_private(cpu,KERNEL_SCHEDULER_YIELD,task,&reply)) return 0;
        result = kernel_scheduler_stop_apply_on(cpu,command->reason - X86_TASK_STOP,command->target,command->token);
        if (result.reply.status != KERNEL_SCHEDULER_OK) return 0;
        return config->reply(task,&result);
    }
    if (command->reason >= X86_TASK_OBSERVE) {
        result = kernel_scheduler_event_apply_on(cpu,command->reason - X86_TASK_OBSERVE,task->index,
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
        return dispatch_apply_private(cpu,KERNEL_SCHEDULER_YIELD,task,&reply);
    }
    if (command->target || command->token) return 0;
    if (!dispatch_apply_private(cpu,command->reason == X86_TASK_EXIT ? KERNEL_SCHEDULER_EXIT :
        KERNEL_SCHEDULER_YIELD,task,&reply)) return 0;
    if (command->reason == X86_TASK_EXIT) {
        if (reply.state != KERNEL_TASK_FINISHED) return 0;
        int collected = config->collect(&reply);
        if (collected == X86_DISPATCH_COLLECT_DEFERRED) return 1;
        if (collected != X86_DISPATCH_COLLECT_DEFAULT ||
            (!retained && !dispatch_apply_private(cpu,KERNEL_SCHEDULER_COLLECT,task,&reply))) return 0;
        if (stats->completed != UINT32_MAX) ++stats->completed;
    }
    return 1;
}

// ====== limit_valid_private ======

// A limit is a bounded diagnostic cap in [minimum, maximum] or X86_DISPATCH_UNLIMITED. Unlimited
// is for continuous interactive sessions: every wait still has its own supervised timeout in
// config->idle, so removing the session cap does not remove a hang detector.
static int limit_valid_private(uint32_t limit, uint32_t minimum, uint32_t maximum)
{
    return limit == X86_DISPATCH_UNLIMITED || (limit >= minimum && limit <= maximum);
}

// ====== dispatch_private ======

static int dispatch_private(uint32_t cpu, uint32_t retained, uint64_t group, uint64_t token,
    const X86_DISPATCH_CONFIG *config, X86_DISPATCH_STATS *stats,
    int (*complete)(void), int (*idle)(void))
{
    int completed = 0;
    if (cpu >= 64 || kernel_scheduler_context_valid_on(cpu) != 1 || entered[cpu] ||
        !config || !stats || !config->run || !config->collect || !config->reply || !config->release_actor ||
        (!config->task_count && !complete) || config->task_count > KERNEL_SCHEDULER_CAPACITY ||
        (!!complete != !!idle) || (complete && (group ||
            config->step_limit != X86_DISPATCH_UNLIMITED || config->idle_limit != X86_DISPATCH_UNLIMITED)) ||
        !limit_valid_private(config->step_limit, config->task_count, 65536U) ||
        !limit_valid_private(config->idle_limit, 0, 32768U) || config->drain_irq > 1 ||
        (cpu && config->drain_irq) || (!config->idle && config->idle_limit) ||
        stats->steps || stats->completed || stats->waiting_count || stats->changed_count ||
        stats->signal_count || stats->stop_waits || stats->idle_calls || stats->failure) return 0;
    if (group && (!retained || config->task_count != 2 ||
        kernel_scheduler_lot_on(cpu,KERNEL_LOT_ENTER,group,token,0).status)) return 0;
    entered[cpu] = 1;
    while (complete || stats->completed < config->task_count) {
        KERNEL_SCHEDULER_REPLY task;
        X86_DISPATCH_COMMAND command = {0};
        stats->failure = 1;
        if (kernel_scheduler_context_valid_on(cpu) != 1) goto failure;
        if (complete && !completed && stats->completed == config->task_count) {
            stats->failure = 0;
            if (kernel_scheduler_apply_on(cpu,KERNEL_SCHEDULER_SELECT,KERNEL_SCHEDULER_NONE,0).status !=
                KERNEL_SCHEDULER_EMPTY || !complete() || kernel_scheduler_context_valid_on(cpu) != 1) {
                stats->failure = 1;
                goto failure;
            }
            completed = 1;
        }
        if (config->step_limit != X86_DISPATCH_UNLIMITED && stats->steps >= config->step_limit) { stats->failure = 3; goto failure; }
        task = kernel_scheduler_apply_on(cpu,KERNEL_SCHEDULER_SELECT,KERNEL_SCHEDULER_NONE,0);
        if (task.status == KERNEL_SCHEDULER_BLOCKED || (completed && task.status == KERNEL_SCHEDULER_EMPTY)) {
            stats->failure = 2;
            int (*wait)(void) = completed ? idle : config->idle;
            if (!wait || (config->idle_limit != X86_DISPATCH_UNLIMITED &&
                stats->idle_calls >= config->idle_limit)) goto failure;
            if (stats->idle_calls != UINT32_MAX) ++stats->idle_calls; // Saturates; never wraps to a small count.
            if (!wait() || kernel_scheduler_context_valid_on(cpu) != 1) goto failure;
            continue; // SELECT again; a provider return is not proof of a READY task.
        }
        stats->failure = 3;
        if (task.status != KERNEL_SCHEDULER_OK || task.index >= KERNEL_SCHEDULER_CAPACITY ||
            !task.generation || task.state != KERNEL_TASK_RUNNING) goto failure;
        if (stats->steps != UINT32_MAX) ++stats->steps;
        stats->failure = 4;
        if (!config->run(&task,&command) || kernel_scheduler_context_valid_on(cpu) != 1) goto failure;
        stats->failure = 5;
        if (!dispatch_command_private(cpu,retained,config,stats,&task,&command)) goto failure;
        stats->failure = 6;
        if (!config->release_actor() || kernel_scheduler_context_valid_on(cpu) != 1) goto failure;
        if (config->drain_irq && kernel_scheduler_irq_drain().reply.status != KERNEL_SCHEDULER_OK) goto failure;
    }
    stats->failure = 7;
    if (group) {
        if (kernel_scheduler_lot_on(cpu,KERNEL_LOT_COMPLETE,group,token,0).status) goto failure;
    } else if (kernel_scheduler_apply_on(cpu,KERNEL_SCHEDULER_SELECT,KERNEL_SCHEDULER_NONE,0).status !=
        KERNEL_SCHEDULER_EMPTY) goto failure;
    stats->failure = 0;
    entered[cpu] = 0;
    return 1;
failure:
    if (group) (void)kernel_scheduler_lot_on(cpu,KERNEL_LOT_FAIL,group,token,0);
    entered[cpu] = 0;
    return 0;
}

// ====== x86_scheduler_dispatch_on ======

int x86_scheduler_dispatch_on(uint32_t cpu, const X86_DISPATCH_CONFIG *config, X86_DISPATCH_STATS *stats)
{
    return dispatch_private(cpu,0,0,0,config,stats,NULL,NULL);
}

// ====== x86_scheduler_dispatch_retained_on ======

int x86_scheduler_dispatch_retained_on(uint32_t cpu, const X86_DISPATCH_CONFIG *config, X86_DISPATCH_STATS *stats)
{
    return dispatch_private(cpu,1,0,0,config,stats,NULL,NULL);
}

// ====== x86_scheduler_dispatch_lot_on ======

int x86_scheduler_dispatch_lot_on(uint32_t cpu, uint64_t group, uint64_t token,
    const X86_DISPATCH_CONFIG *config, X86_DISPATCH_STATS *stats)
{
    if (!group || !token) return 0;
    return dispatch_private(cpu,1,group,token,config,stats,NULL,NULL);
}

// ====== x86_scheduler_dispatch ======

int x86_scheduler_dispatch(const X86_DISPATCH_CONFIG *config, X86_DISPATCH_STATS *stats)
{
    return x86_scheduler_dispatch_on(0,config,stats);
}

// ====== x86_scheduler_dispatch_continuous ======

int x86_scheduler_dispatch_continuous(const X86_DISPATCH_CONFIG *config, X86_DISPATCH_STATS *stats,
    int (*complete)(void), int (*idle)(void))
{
    if (!complete || !idle) return 0;
    return dispatch_private(0,0,0,0,config,stats,complete,idle);
}

// ====== x86_scheduler_dispatch_continuous_on ======

int x86_scheduler_dispatch_continuous_on(uint32_t cpu, const X86_DISPATCH_CONFIG *config,
    X86_DISPATCH_STATS *stats, int (*complete)(void), int (*idle)(void)) {
    if (!cpu || !complete || !idle) return 0;
    return dispatch_private(cpu,1,0,0,config,stats,complete,idle);
}
