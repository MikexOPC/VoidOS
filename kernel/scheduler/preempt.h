#ifndef OS_KERNEL_SCHEDULER_PREEMPT_H
#define OS_KERNEL_SCHEDULER_PREEMPT_H

#include <stdint.h>

#define KERNEL_PREEMPT_LINE_TICKS 4U

#define KERNEL_PREEMPT_IGNORE  0U
#define KERNEL_PREEMPT_DEFER   1U
#define KERNEL_PREEMPT_SUSPEND 2U

typedef struct {
    uint64_t  task;
    uint32_t  interrupts_enabled;
    uint32_t  lock_depth;
    uint32_t  line_open;
    uint32_t  slice_over;
} KERNEL_PREEMPT_INPUT;

typedef struct {
    uint64_t  task;
    uint64_t  ticks;
    uint64_t  ignored;
    uint64_t  preempted;
    uint64_t  deferred_lock;
    uint64_t  deferred_line;
    uint64_t  overruns;
    uint32_t  line_ticks;
    uint32_t  reserved;
} KERNEL_PREEMPT_STATE;

uint32_t kernel_preempt_decide(KERNEL_PREEMPT_STATE *state, const KERNEL_PREEMPT_INPUT *input);

#endif
