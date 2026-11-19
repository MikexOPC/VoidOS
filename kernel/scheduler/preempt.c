#include "preempt.h"

// ====== count ======

static void count(uint64_t *counter)
{
    if (*counter != UINT64_MAX) ++*counter;
}

// ====== kernel_preempt_decide ======

uint32_t kernel_preempt_decide(KERNEL_PREEMPT_STATE *state, const KERNEL_PREEMPT_INPUT *input)
{
    if (!state || !input) return KERNEL_PREEMPT_IGNORE;
    count(&state->ticks);
    if (!input->task || !input->interrupts_enabled) {
        state->task = 0;
        state->line_ticks = 0;
        count(&state->ignored);
        return KERNEL_PREEMPT_IGNORE;
    }
    if (state->task != input->task) {
        state->task = input->task;
        state->line_ticks = 0;
    }
    if (!input->slice_over) {
        state->line_ticks = 0;
        count(&state->ignored);
        return KERNEL_PREEMPT_IGNORE;
    }
    if (input->lock_depth) {
        count(&state->deferred_lock);
        return KERNEL_PREEMPT_DEFER;
    }
    if (input->line_open && state->line_ticks < KERNEL_PREEMPT_LINE_TICKS) {
        ++state->line_ticks;
        count(&state->deferred_line);
        return KERNEL_PREEMPT_DEFER;
    }
    if (input->line_open) count(&state->overruns);
    count(&state->preempted);
    state->line_ticks = 0;
    return KERNEL_PREEMPT_SUSPEND;
}

// ====== kernel_preempt_line_complete ======

uint32_t kernel_preempt_line_complete(KERNEL_PREEMPT_STATE *state, uint64_t task,
    uint32_t interrupts_enabled, uint32_t lock_depth) {
    if (!state ||
        !task ||
        state->task != task ||
        !state->line_ticks ||
        !interrupts_enabled ||
        lock_depth
    )
        return KERNEL_PREEMPT_IGNORE;

    state->line_ticks = 0;
    count(&state->preempted);
    return KERNEL_PREEMPT_SUSPEND;
}
