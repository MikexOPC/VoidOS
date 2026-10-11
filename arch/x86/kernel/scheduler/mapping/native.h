#ifndef OS_X86_SCHEDULER_NATIVE_H
#define OS_X86_SCHEDULER_NATIVE_H
#include "../stacks.h"
#include "arch/x86/kernel/stack/main.h"
#include "arch/x86/kernel/exceptions/terminal.h"
#include "arch/x86/mm/window/access.h"

// ====== native_binding ======

int x86_scheduler_bind(X86_TASK_STACK *owners, X86_TASK_CONTEXT *contexts,
                      KERNEL_PHYSICAL_ALLOCATOR *pool, uint64_t root);
int x86_scheduler_native_gate(void);
uint64_t x86_scheduler_live_stack_pages(void);
uint64_t x86_scheduler_live_stack_pages_for(const KERNEL_PHYSICAL_ALLOCATOR *pool, uint64_t root);
int x86_scheduler_code_valid(uint64_t target);
int x86_scheduler_dispatcher_context_is(const X86_TASK_CONTEXT *context);
uint64_t x86_scheduler_cpu_permissions(void);
void x86_scheduler_unbind(void);
volatile unsigned char *x86_scheduler_native_bytes(uint32_t index);
volatile uint64_t *x86_scheduler_code_entries(uint32_t level, uint64_t target);
#if defined(KERNEL_TEST_SCHEDULER_FAULT) && KERNEL_TEST_SCHEDULER_FAULT == 5
uint32_t x86_scheduler_zero_fault_used(void);
#endif

#endif
