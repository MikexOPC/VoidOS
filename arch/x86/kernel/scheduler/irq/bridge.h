#ifndef X86_SCHEDULER_IRQ_BRIDGE_H
#define X86_SCHEDULER_IRQ_BRIDGE_H
#include "arch/x86/kernel/timer/lapic/timer.h"
// ====== timer_owned_session ======

#ifdef KERNEL_TEST_SCHEDULER_PERIODIC
int x86_scheduler_periodic_run(const X86_LAPIC_TIMER *source);
void x86_scheduler_periodic_task(uint32_t index);
int x86_scheduler_periodic_verified(void);
int x86_scheduler_run_periodic(void);
#endif
int x86_scheduler_timer_tasks(X86_LAPIC_TIMER *source);
int x86_scheduler_timer_notify(const X86_LAPIC_TIMER *source);
uint64_t x86_scheduler_irq_stack_top(uint64_t frame);
int x86_scheduler_irq_record(uint64_t vector, uint64_t rip, uint64_t rsp, uint64_t flags);
int x86_scheduler_irq_compute(const volatile uint32_t *source, uint32_t port,
    uint32_t mask, uint32_t ticks);
// 0 = rejected/corrupted, 1 = input, 2 = bounded empty poll with intact context.
int x86_scheduler_irq_poll(const volatile uint32_t *source, uint32_t port,
    uint32_t mask, uint32_t ticks);
#endif
