#ifndef X86_SCHEDULER_PROFILES_H
#define X86_SCHEDULER_PROFILES_H
#include "../dispatcher/engine.h"

typedef struct {
    uint32_t  task_count;
    uint32_t  step_limit;
    uint32_t  idle_limit;
    uint32_t  drain_irq;
    void (*entry)(uint32_t);
    int (*idle)(void);
} X86_SCHEDULER_PROFILE;

// ====== diagnostic_profiles_contract ======

const X86_SCHEDULER_PROFILE *x86_scheduler_profile(uint32_t id);
int x86_scheduler_profile_complete(uint32_t id, const X86_DISPATCH_STATS *stats);
int x86_scheduler_profile_blocked(void);
#endif
