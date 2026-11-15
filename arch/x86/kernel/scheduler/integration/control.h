#ifndef X86_SCHEDULER_CONTROL_H
#define X86_SCHEDULER_CONTROL_H
#include "../mapping/native.h"
#include "kernel/scheduler/scheduler.h"
#include "../dispatcher/engine.h"
// ====== private_task_contract ======

typedef void (*X86_TASK_ENTRY)(uint32_t index);
extern X86_TASK_STACK x86_task_stacks[8];
extern X86_TASK_CONTEXT x86_task_contexts[8];
extern uint32_t x86_scheduler_machine_mask;
int x86_scheduler_binding_open(KERNEL_PHYSICAL_ALLOCATOR *pool, uint64_t root);
int x86_scheduler_task_assign(uint32_t index, uint32_t cpu, X86_TASK_ENTRY entry);
int x86_scheduler_dispatch_run(const KERNEL_SCHEDULER_REPLY *task, X86_DISPATCH_COMMAND *command);
int x86_scheduler_dispatch_reply(const KERNEL_SCHEDULER_REPLY *task, const KERNEL_EVENT_REPLY *reply);
int x86_scheduler_dispatch_release(void);
void x86_scheduler_irq_suspend(void);
void x86_scheduler_tick_suspend(void);
__declspec(noreturn) void x86_scheduler_exit(void);
uint32_t x86_scheduler_index(void);
uint64_t x86_scheduler_generation(void);
int x86_scheduler_task_valid(void);
int x86_scheduler_driver_task_valid(void);
void x86_scheduler_yield(void);
KERNEL_EVENT_REPLY x86_scheduler_event_request(uint32_t reason, uint32_t id, uint64_t token);
void x86_scheduler_workload(void);
int x86_scheduler_workload_complete(void);
int x86_scheduler_run_irq(void);
uint64_t x86_scheduler_irq_stack_top(uint64_t frame);
void x86_scheduler_irq_task(uint32_t index);
int x86_scheduler_irq_verified(void);
int x86_scheduler_irq_timeout_valid(void);
int x86_scheduler_report(uint64_t root, uint64_t before, const KERNEL_PHYSICAL_ALLOCATOR *pool, uint32_t steps, uint32_t profile);
void x86_scheduler_test_fault(uint32_t point);
const X86_DISPATCH_STATS *x86_scheduler_session_stats(void);
int x86_scheduler_retained_valid(void);
int x86_scheduler_resume_valid(const X86_TASK_CONTEXT *context, uint32_t index, uint32_t machine_mask);
#endif
