#include "batch.h"
#include "arch/x86/kernel/cpu/percpu/service/native/native.h"
#include "arch/x86/kernel/cpu/percpu/resources/resources.h"

// ====== cpu_private ======

static uint32_t cpu_private(void)
{
    const X86_CPU_LOCAL *owner = x86_percpu_current_local();
    return owner ? owner->Index : UINT32_MAX;
}

// ====== xat_scope_bind ======

int xat_scope_bind(void)
{
    X86_AP_SCOPE scope = {xat_record.Root,1,xat_pool,xat_arena,XAT_APS,0};
    return x86_ap_scope_bind(&scope);
}

// ====== xat_ap_actor ======

int xat_ap_actor(void) { return x86_ap_actor(); }

// ====== xat_ap_gate ======

int xat_ap_gate(const KERNEL_PHYSICAL_ALLOCATOR *pool, uint64_t root, const X86_TASK_CPU *cpu)
{
    return x86_ap_gate(pool,root,cpu);
}

// ====== xat_dispatcher_gate ======

int xat_dispatcher_gate(void) { return x86_ap_dispatcher_gate(); }

// ====== xat_stack_authority ======

int xat_stack_authority(const X86_TASK_CONTEXT *context, uint64_t base, uint64_t bytes)
{
    X86_SCHEDULER_LOCAL *local = x86_scheduler_local_current();
    if (!xat_ap_actor() || !local || bytes != 65536 || !x86_scheduler_native_gate()) return 0;
    if (context == &local->dispatcher && base == X86_AP_STACK_BASE(cpu_private()))
        return x86_ap_dispatcher_authority();
    for (uint32_t index = 0; index < XAT_TASKS; ++index) {
        if (XAT_CPU(index) != cpu_private()) continue;
        X86_TASK_STACK *owner = &x86_task_stacks[index];
        if (context == &x86_task_contexts[index] && base == x86_task_stack_base(index))
            return owner->self == (uint64_t)(uintptr_t)owner && owner->pool == xat_pool &&
                owner->root == xat_record.Root && owner->index == index &&
                owner->generation == xat_record.Generations[index] && owner->stage == X86_TASK_STACK_READY &&
                owner->lease.Id && owner->lease.Owner == xat_pool->Self && owner->lease.Pages == 16 &&
                x86_task_stack_mapping_valid(owner,1) && kernel_physical_is_pinned(xat_pool,&owner->lease) == 1;
    }
    return 0;
}

// ====== xat_task_valid ======

int xat_task_valid(void)
{
    X86_SCHEDULER_LOCAL *local = x86_scheduler_local_current();
    if (!local || local->phase != 2 || local->current >= XAT_TASKS ||
        XAT_CPU(local->current) != cpu_private() ||
        local->generation != xat_record.Generations[local->current]) return 0;
    uint64_t base = x86_task_stack_base(local->current), rsp = x86_kernel_stack_pointer();
    return rsp >= base + 4096 && rsp < base + 65536 && x86_scheduler_task_valid() &&
        xat_stack_authority(&x86_task_contexts[local->current],base,65536);
}

// ====== xat_irq_frame ======

int xat_irq_frame(uint32_t cpu, const X86_IRQ_FRAME *frame)
{
    X86_SCHEDULER_LOCAL *local = x86_scheduler_local_current();
    if (cpu < 1 || cpu > XAT_APS || cpu != cpu_private() || !local || local->current >= XAT_TASKS || !xat_task_valid()) return 0;
    uint64_t base = x86_task_stack_base(local->current);
    if (x86_irq_saved_area((uint64_t)(uintptr_t)frame,base + 4096,base + 65536) != 1 ||
        xis_task_return_valid((uint64_t)(uintptr_t)frame,frame->Rsp,frame->Cs,frame->Flags,frame->Ss) != 1 ||
        !x86_scheduler_code_valid(frame->Rip)) return 0;
#ifdef KERNEL_TEST_AP_PREEMPTION
    if (frame->Vector == XIS_TIMER_VECTOR) return 1;
#endif
    if (frame->Vector != XIS_VECTOR) return frame->Vector == 0xff;
#ifdef KERNEL_TEST_AP_CONCURRENT
    XAT_RECORD *record = xat_cpu_record();
#else
    XAT_RECORD *record = &xat_record;
#endif
    uint64_t count = ks_atomic_load_relaxed(&record->Irqs);
    if (count == UINT64_MAX) return 0;
    record->FrameRsp = frame->Rsp; record->FrameIndex = local->current;
    record->FrameGeneration = local->generation;
    record->IrqFlags = frame->Flags; record->IrqVector = frame->Vector;
    ks_atomic_store_release(&record->IrqMask,record->IrqMask | (1ULL << local->current));
    ks_atomic_store_release(&record->Irqs,count + 1);
    return 1;
}
