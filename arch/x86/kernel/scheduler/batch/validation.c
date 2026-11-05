#include "batch.h"
#include "arch/x86/kernel/cpu/percpu/service/native/native.h"
#include "arch/x86/kernel/cpu/percpu/resources/resources.h"

// ====== cpu_private ======

static uint32_t cpu_private(void)
{
    const X86_CPU_LOCAL *owner = x86_percpu_current_local();
    return owner ? owner->Index : UINT32_MAX;
}

// ====== xat_ap_actor ======

int xat_ap_actor(void)
{
    const X86_CPU_LOCAL *owner = x86_percpu_current_local();
    return owner && owner->Index >= 1 && owner->Index <= XAT_APS && owner == xp_panic_native.Contexts[owner->Index] &&
        owner->State == X86_PERCPU_ACTIVE && owner->Self == (uint64_t)(uintptr_t)owner &&
        xad_value(owner->Index,xat_record.Root) == (1ULL << 3 | XAD_RUNNING);
}

// ====== xat_ap_gate ======

// Intel SDM 078US §§4.5/4.6: checked recursive parents, P0/RW1/US2/XD63.
// §11.4.4: APIC_BASE.BSP8=0, global enable11=1, x2APIC10=0 in this profile.
int xat_ap_gate(const KERNEL_PHYSICAL_ALLOCATOR *pool, uint64_t root, const X86_TASK_CPU *cpu)
{
    if (!xat_ap_actor() || !cpu || !xp_panic_native.Lapic || pool != xat_pool || root != xat_record.Root ||
        !x86_scheduler_ap_cpu_valid(cpu,root,xp_panic_native.Ids[cpu_private()]) ||
        (cpu->cr4 & ((1ULL << 7) | (1ULL << 17))) ||
        cpu->apic_base != (xp_panic_native.Lapic->Physical | 0x800ULL)) return 0;
    const X86_AP_AUTH *auth = x86_ap_owner();
    if (!auth || auth->Self != xat_arena.Owner || auth->Root != root || auth->Cpus != xat_arena.Cpus ||
        auth->Ready != 1 || auth->Reserved || auth->PhysicalBits != pool->PhysicalBits ||
        auth->Data.Pages != xat_arena.Pages || auth->Tables.Pages != xat_arena.Tables ||
        auth->Data.Owner != pool->Self || auth->Tables.Owner != pool->Self ||
        auth->Data.Id != xat_arena.DataId || auth->Data.Base != xat_arena.Base ||
        auth->Tables.Base != xat_arena.TableBase || auth->Tables.Id != xat_arena.TableId) return 0;
    X86_GDT_POINTER gdtr;
    X86_KERNEL_IDTR idtr;
    if (xis_native.Idtr.Bytes[0] != 255 || xis_native.Idtr.Bytes[1] != 15) return 0;
    x86_kernel_segments_read(&gdtr); x86_exception_read_idt(&idtr);
    uint64_t gdt_base = 0;
    for (uint32_t byte = 0; byte < 8; ++byte) gdt_base |= (uint64_t)gdtr.Bytes[byte + 2] << (byte * 8);
    if (gdtr.Bytes[0] != 39 || gdtr.Bytes[1] || gdt_base != X86_AP_METADATA(cpu_private())) return 0;
    for (uint32_t byte = 0; byte < 10; ++byte)
        if (idtr.Bytes[byte] != xis_native.Idtr.Bytes[byte]) return 0;
    uint64_t base = X86_AP_STACK_BASE(cpu_private());
    for (uint32_t level = 0; level < 3; ++level) {
        volatile uint64_t *entries = x86_scheduler_code_entries(level,base);
        uint32_t at = (uint32_t)((base >> (39 - level * 9)) & 511);
        if (!entries || (entries[at] & ~0x60ULL) != ((xat_arena.TableBase + level * 4096ULL) | 3)) return 0;
    }
    volatile uint64_t *leaves = x86_scheduler_code_entries(3,base);
    uint32_t at = (uint32_t)((base >> 12) & 511);
    if (!leaves || leaves[at - 1] || leaves[at + 16]) return 0;
    for (uint32_t page = 0; page < 16; ++page)
        if (x86_ap_leaf(leaves[at + page],base + page * 4096ULL) != 10) return 0;
    return 1;
}

// ====== xat_dispatcher_gate ======

int xat_dispatcher_gate(void)
{
    X86_SCHEDULER_LOCAL *local = x86_scheduler_local_current();
    uint64_t rsp = x86_kernel_stack_pointer();
    return xat_ap_actor() && local && local->phase == 1 &&
        rsp >= X86_AP_STACK_FLOOR(cpu_private()) && rsp < X86_AP_FRAME(cpu_private()) - XIS_IDLE_FRAME_BYTES &&
        x86_scheduler_native_gate();
}

// ====== xat_stack_authority ======

int xat_stack_authority(const X86_TASK_CONTEXT *context, uint64_t base, uint64_t bytes)
{
    X86_SCHEDULER_LOCAL *local = x86_scheduler_local_current();
    if (!xat_ap_actor() || !local || bytes != 65536 || !x86_scheduler_native_gate()) return 0;
    if (context == &local->dispatcher && base == X86_AP_STACK_BASE(cpu_private())) {
        const X86_AP_AUTH *auth = x86_ap_owner();
        return auth && kernel_physical_is_pinned(xat_pool,&auth->Data) == 1 &&
            kernel_physical_is_pinned(xat_pool,&auth->Tables) == 1;
    }
    for (uint32_t index = 0; index < XAT_TASKS; ++index) {
        if (XAT_CPU(index) != cpu_private()) continue;
        X86_TASK_STACK *owner = &xat_stacks[index];
        if (context == &xat_contexts[index] && base == x86_task_stack_base(index))
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
    return rsp >= base + 4096 && rsp < base + 65536 &&
        xat_stack_authority(&xat_contexts[local->current],base,65536);
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
    if (frame->Vector == 0xd1) return 1;
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
