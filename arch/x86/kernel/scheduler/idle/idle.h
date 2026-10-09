#ifndef X86_SCHEDULER_IDLE_H
#define X86_SCHEDULER_IDLE_H
#include "../irq/bridge.h"

// ====== dispatcher_idle_contract ======

int x86_scheduler_timer_idle_tasks(X86_LAPIC_TIMER *source);
int x86_scheduler_idle_notify(const X86_LAPIC_TIMER *source);
int x86_scheduler_idle_wait(void);
void x86_scheduler_idle_task(uint32_t index);
int x86_scheduler_idle_verified(void);
uint32_t x86_scheduler_idle_slept(void);
uint32_t x86_scheduler_idle_failure(void);
int x86_scheduler_run_idle(void);
int x86_scheduler_run_idle_repeat(void);
int x86_scheduler_timer_idle_repeat(X86_LAPIC_TIMER *source);
int x86_scheduler_idle_retained_valid(void);
#endif
