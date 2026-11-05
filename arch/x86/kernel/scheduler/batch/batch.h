#ifndef X86_SCHEDULER_BATCH_H
#define X86_SCHEDULER_BATCH_H
#include "../integration/local.h"
#include "../mapping/native.h"
#include "../admission/admission.h"
#include "arch/x86/kernel/cpu/percpu/resources/paging/authority.h"
#include "arch/x86/kernel/interrupts/return/table.h"

typedef __declspec(align(64)) struct {
    uint64_t Self, Root, Pool, Budget, Held, Final;
    uint64_t Progress[2], Checksums[2], Floating[2];
    uint64_t Irqs, IrqMask, FrameRsp, FrameIndex, FrameGeneration;
    uint64_t Generations[2], Steps, Completed, Bases[2], Local, Contexts[2];
    uint64_t Retained, Collected, IrqFlags, IrqVector, DeniedForeignCpu, Aborted;
} XAT_RECORD;
_Static_assert(sizeof(XAT_RECORD) == 256,"AP batch evidence ABI");
extern XAT_RECORD xat_record;
extern X86_TASK_STACK xat_stacks[8];
extern X86_TASK_CONTEXT xat_contexts[8];
extern KERNEL_PHYSICAL_ALLOCATOR *xat_pool;
extern X86_AP_SNAPSHOT xat_arena;
extern uint32_t xat_mask;
KERNEL_PHYSICAL_ALLOCATOR *x86_scheduler_batch_resources(uint64_t *root);
int xat_ap_actor(void);
int xat_ap_gate(const KERNEL_PHYSICAL_ALLOCATOR *pool, uint64_t root, const X86_TASK_CPU *cpu);
int xat_dispatcher_gate(void);
int xat_stack_authority(const X86_TASK_CONTEXT *context, uint64_t base, uint64_t bytes);
int xat_task_valid(void);
int xat_irq_frame(uint32_t cpu, const X86_IRQ_FRAME *frame);
void xat_prepare(void);
void xat_run(void);
void xat_cleanup(void);
void xat_yield(void);
__declspec(noreturn) void xat_task_start(void);
__declspec(noreturn) void xat_abort(void);
#endif
