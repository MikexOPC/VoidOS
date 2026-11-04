#ifndef X86_SCHEDULER_DISPATCH_ENGINE_H
#define X86_SCHEDULER_DISPATCH_ENGINE_H
#include "kernel/scheduler/scheduler.h"

#define X86_TASK_YIELD 1U
#define X86_TASK_EXIT 2U
#define X86_TASK_OBSERVE 3U
#define X86_TASK_WAIT 4U
#define X86_TASK_SIGNAL 5U
#define X86_TASK_STOP 6U
#define X86_TASK_STOP_QUERY 7U
#define X86_TASK_PREEMPT 8U

// step_limit/idle_limit value meaning "no cap": a continuous interactive session.
#define X86_DISPATCH_UNLIMITED 0xFFFFFFFFU

typedef struct {
    uint32_t  reason;
    uint32_t  target;
    uint64_t  token;
} X86_DISPATCH_COMMAND;

typedef struct {
    uint32_t  steps;
    uint32_t  completed;
    uint32_t  waiting_count;
    uint32_t  changed_count;
    uint32_t  signal_count;
    uint32_t  stop_waits;
    uint32_t  idle_calls;
    uint32_t  failure;
} X86_DISPATCH_STATS;

typedef struct {
    uint32_t  task_count;
    uint32_t  step_limit;
    uint32_t  idle_limit;
    uint32_t  drain_irq;
    int (*run)(const KERNEL_SCHEDULER_REPLY *, X86_DISPATCH_COMMAND *);
    int (*collect)(const KERNEL_SCHEDULER_REPLY *);
    int (*reply)(const KERNEL_SCHEDULER_REPLY *, const KERNEL_EVENT_REPLY *);
    int (*release_actor)(void);
    int (*idle)(void);
} X86_DISPATCH_CONFIG;

// ====== dispatcher_engine_contract ======

// Private BSP/IF=0 API, immutable trusted config and owned stable zeroed stats.
// Caller has published exactly task_count owned tasks and has no RUNNING task.
// Native callbacks validate ownership/ABI and may not recursively enter engine.
// Failure preserves resources/state; caller decides terminal handling, no retry
// or implicit cleanup. idle must be supervised, finish with IF=0 and no borrow.
int x86_scheduler_dispatch(const X86_DISPATCH_CONFIG *config, X86_DISPATCH_STATS *stats);
#endif
