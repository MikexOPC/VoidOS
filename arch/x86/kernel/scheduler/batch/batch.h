#ifndef X86_SCHEDULER_BATCH_H
#define X86_SCHEDULER_BATCH_H
#include "../integration/local.h"
#include "../mapping/native.h"
#include "../admission/admission.h"
#include "arch/x86/kernel/cpu/percpu/resources/paging/authority.h"
#include "arch/x86/kernel/interrupts/return/table.h"
#ifdef KERNEL_TEST_AP_BATCH_STRESS
#define XAT_PREEMPT_TARGET 8U
#define XAT_REQUEST_ROUNDS 8U
#else
#define XAT_PREEMPT_TARGET 3U
#define XAT_REQUEST_ROUNDS 1U
#endif
#ifdef KERNEL_TEST_AP_CONCURRENT
#ifdef KERNEL_TEST_AP_FOUR
#define XAT_TASKS 8U
#define XAT_APS 4U
#else
#define XAT_TASKS 4U
#define XAT_APS 2U
#endif
#define XAT_CPU(index) ((index) / 2U + 1U)
#else
#define XAT_TASKS 2U
#define XAT_APS 1U
#define XAT_CPU(index) 1U
#endif
#define XAT_PARTICIPANTS ((1ULL << (XAT_APS + 1)) - 2)

typedef __declspec(align(64)) struct {
    uint64_t Self, Root, Pool, Budget, Held, Final;
    uint64_t Progress[XAT_TASKS], Checksums[XAT_TASKS], Floating[XAT_TASKS];
    uint64_t Irqs, IrqMask, FrameRsp, FrameIndex, FrameGeneration;
    uint64_t Generations[XAT_TASKS], Steps, Completed, Bases[XAT_TASKS], Local, Contexts[XAT_TASKS];
    uint64_t Retained, Collected, IrqFlags, IrqVector, DeniedForeignCpu, Aborted;
#ifdef KERNEL_TEST_AP_CONCURRENT
    uint64_t Reserved[4];
#endif
} XAT_RECORD;
_Static_assert(sizeof(XAT_RECORD) == (XAT_TASKS == 8 ? 576 : XAT_TASKS == 4 ? 384 : 256),"AP batch evidence ABI");
extern XAT_RECORD xat_record;
#ifdef KERNEL_TEST_AP_CONCURRENT
extern XAT_RECORD xat_second_record;
XAT_RECORD *xat_cpu_record(void);
XAT_RECORD *xat_record_at(uint32_t cpu);
void xat_concurrent_barrier(uint32_t index);
#endif
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
#ifdef KERNEL_TEST_AP_PREEMPTION
typedef __declspec(align(64)) struct {
    uint64_t Self, Ticks[2], Resumed[2], Total, Stopped, Late;
    uint64_t Frame[2], Rsp[2], Rip[2], Reload, Reserved;
} XAT_PREEMPT;
extern XAT_PREEMPT xat_preempt;
#ifdef KERNEL_TEST_AP_CONCURRENT
extern XAT_PREEMPT xat_preempt_second;
XAT_PREEMPT *xat_preemption_record(void);
XAT_PREEMPT *xat_preemption_at(uint32_t cpu);
#endif
uint32_t x86_timer_diagnostic_reload(void);
void xat_preempt_work(uint32_t index);
void xat_preempt_interrupt(const X86_IRQ_FRAME *frame);
void xat_preempt_after(const X86_IRQ_FRAME *frame);
int xat_preempt_quiet(void);
#endif
#endif
