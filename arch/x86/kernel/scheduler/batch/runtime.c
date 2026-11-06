#include "batch.h"
#include "arch/x86/kernel/cpu/percpu/service/native/native.h"
#include "kernel/diagnostics/log.h"
#include "kernel/scheduler/lot.h"
#ifdef KERNEL_TEST_AP_CONSOLE
#include "test/kernel/console/video/ap/channel.h"
#endif

XAT_RECORD xat_record;
X86_TASK_STACK xat_stacks[8];
X86_TASK_CONTEXT xat_contexts[8];
KERNEL_PHYSICAL_ALLOCATOR *xat_pool;
X86_AP_SNAPSHOT xat_arena;
uint32_t xat_mask;

// ====== xat_abort ======

__declspec(noreturn) void xat_abort(void)
{
    const X86_CPU_LOCAL *local = x86_percpu_current_local();
    (void)xad_fail(local && local->Index ? local->Index : 1,local ? local->Index : UINT32_MAX,1,xat_record.Root);
    xis_native_abort(0x4b34415042415443ULL);
}

// ====== apply_private ======

static KERNEL_SCHEDULER_REPLY apply_private(uint32_t cpu, uint32_t operation, uint32_t index)
{
    KERNEL_SCHEDULER_REPLY reply = kernel_scheduler_apply_on(cpu,operation,
        index < XAT_TASKS ? index : KERNEL_SCHEDULER_NONE,index < XAT_TASKS ? xat_record.Generations[index] : 0);
    if (reply.status != KERNEL_SCHEDULER_OK) xat_abort();
    return reply;
}

// ====== rollback_private ======

__declspec(noreturn) static void rollback_private(void)
{
    for (uint32_t index = 0; index < XAT_TASKS; ++index) {
        if (!xat_record.Generations[index]) continue;
        if (xat_stacks[index].self && !x86_task_stack_release(&xat_stacks[index],index,xat_record.Generations[index]))
            xat_abort();
        (void)apply_private(0,KERNEL_SCHEDULER_ABORT,index);
        ++xat_record.Aborted;
    }
    if (!kernel_physical_budget_read(xat_pool,&xat_record.Final) ||
        xat_record.Final != xat_record.Budget ||
        kernel_scheduler_apply(KERNEL_SCHEDULER_SELECT,KERNEL_SCHEDULER_NONE,0).status != KERNEL_SCHEDULER_EMPTY)
        xat_abort();
#ifdef KERNEL_TEST_AP_BATCH_ROLLBACK
    if (!kernel_text("KERNEL_AP_BATCH_RECORD: ") || !kernel_number((uint64_t)(uintptr_t)&xat_record) ||
        !kernel_text("\nKERNEL_AP_BATCH_PTE: ") || !kernel_number((uint64_t)(uintptr_t)&x86_stack_entries(3)[320]) ||
        !kernel_text("\n") || !kernel_log(2,"KERNEL_AP_BATCH_ROLLBACK_OK")) xat_abort();
#endif
    xat_abort();
}

// ====== xat_prepare ======

void xat_prepare(void)
{
    unsigned char __declspec(align(16)) fx[512];
    xat_record.Self = (uint64_t)(uintptr_t)&xat_record;
    xat_pool = x86_scheduler_batch_resources(&xat_record.Root);
    X86_SCHEDULER_LOCAL *local = x86_scheduler_local_current();
    if (!xat_pool || !local || xad_value(1,xat_record.Root) != (1ULL << 3 | XAD_ADMITTED) ||
        !kernel_physical_budget_read(xat_pool,&xat_record.Budget) ||
        !x86_ap_inputs(xat_pool,xat_record.Root,(uint64_t)(uintptr_t)&xat_arena,sizeof(xat_arena),&xat_arena) ||
        !x86_scheduler_bind(xat_stacks,xat_contexts,xat_pool,xat_record.Root)) xat_abort();
    local->phase = 1;
    if (kernel_scheduler_configure(xis_native.Count,1) || !x86_scheduler_fp_template(fx)) xat_abort();
    xat_mask = (uint32_t)fx[28] | (uint32_t)fx[29] << 8 | (uint32_t)fx[30] << 16 | (uint32_t)fx[31] << 24;
    if (!xat_mask) xat_mask = 0xffbf;
    xat_record.Pool = (uint64_t)(uintptr_t)xat_pool;
    for (uint32_t index = 0; index < XAT_TASKS; ++index) {
        KERNEL_SCHEDULER_REPLY reply = apply_private(0,KERNEL_SCHEDULER_RESERVE,KERNEL_SCHEDULER_NONE);
        if (reply.index != index || reply.state != KERNEL_TASK_RESERVED || !reply.generation) xat_abort();
        xat_record.Generations[index] = reply.generation;
        xat_record.Bases[index] = x86_task_stack_base(index);
        xat_record.Contexts[index] = (uint64_t)(uintptr_t)&xat_contexts[index];
#ifdef KERNEL_TEST_AP_BATCH_ROLLBACK
        if (index == 1) {
            if (!kernel_physical_budget_read(xat_pool,&xat_record.Held) ||
                xat_record.Budget < 16 || xat_record.Held != xat_record.Budget - 16 ||
                xat_stacks[0].stage != X86_TASK_STACK_READY || xat_stacks[1].self) xat_abort();
            rollback_private();
        }
#endif
        if (kernel_scheduler_affinity_on(0,index,reply.generation,1ULL << XAT_CPU(index)) ||
            !x86_task_stack_acquire(&xat_stacks[index],xat_pool,xat_record.Root,index,reply.generation) ||
            !x86_scheduler_context_prepare(&xat_contexts[index],xat_record.Bases[index],65536)) rollback_private();
    }
    for (uint32_t index = 0; index < XAT_TASKS; ++index)
        if (apply_private(0,KERNEL_SCHEDULER_PUBLISH,index).state != KERNEL_TASK_READY) xat_abort();
    if (!xis_native_shootdown()) xat_abort();
    if (!kernel_physical_budget_read(xat_pool,&xat_record.Held) ||
        xat_record.Budget < 16 * XAT_TASKS || xat_record.Held != xat_record.Budget - 16 * XAT_TASKS) xat_abort();
}

// ====== run_private ======

static int run_private(const KERNEL_SCHEDULER_REPLY *task, X86_DISPATCH_COMMAND *command)
{
    X86_SCHEDULER_LOCAL *local = x86_scheduler_local_current();
    if (!local || !xat_dispatcher_gate() || local->current != KERNEL_SCHEDULER_NONE || task->index >= XAT_TASKS ||
        XAT_CPU(task->index) != xis_native_index() ||
        task->generation != xat_record.Generations[task->index] ||
        !x86_scheduler_context_resume_valid(&xat_contexts[task->index],xat_record.Bases[task->index],65536,
            X86_KERNEL_IMAGE_BASE,X86_KERNEL_IMAGE_LIMIT,xat_mask)) return 0;
    local->current = task->index; local->generation = task->generation; local->event = 0; local->phase = 2;
    x86_scheduler_context_switch(&local->dispatcher,&xat_contexts[task->index]);
    local->phase = 1;
    if (!xat_dispatcher_gate() || local->current != task->index || local->generation != task->generation) return 0;
    command->reason = local->event; local->event = 0;
#ifdef KERNEL_TEST_AP_PREEMPTION
    if (command->reason == X86_TASK_PREEMPT) return 1;
#endif
    return command->reason == X86_TASK_YIELD || command->reason == X86_TASK_EXIT;
}

// ====== retain_private ======

static int retain_private(const KERNEL_SCHEDULER_REPLY *task)
{
    return task->index < XAT_TASKS && task->state == KERNEL_TASK_FINISHED &&
        task->generation == xat_record.Generations[task->index] &&
        xat_stack_authority(&xat_contexts[task->index],xat_record.Bases[task->index],65536);
}

// ====== reply_private ======

static int reply_private(const KERNEL_SCHEDULER_REPLY *task, const KERNEL_EVENT_REPLY *reply)
{ (void)task; (void)reply; return 0; }

// ====== release_private ======

static int release_private(void)
{
    X86_SCHEDULER_LOCAL *local = x86_scheduler_local_current();
    if (!local || !xat_dispatcher_gate()) return 0;
    local->current = KERNEL_SCHEDULER_NONE; local->generation = 0;
    return 1;
}

// ====== xat_run ======

void xat_run(void)
{
    X86_SCHEDULER_LOCAL *local = x86_scheduler_local_current();
    if (!local || local->phase || local->current != KERNEL_SCHEDULER_NONE || !xat_ap_actor()) xat_abort();
    local->phase = 1;
    if (kernel_scheduler_apply_on(0,KERNEL_SCHEDULER_CURRENT,KERNEL_SCHEDULER_NONE,0).status != KERNEL_SCHEDULER_DENIED)
        xat_abort();
#ifdef KERNEL_TEST_AP_CONCURRENT
    XAT_RECORD *record = xat_cpu_record();
    uint32_t cpu = xis_native_index();
#else
    XAT_RECORD *record = &xat_record;
    uint32_t cpu = 1;
#endif
    record->DeniedForeignCpu = 1;
    X86_DISPATCH_CONFIG config = {2,8192,0,0,run_private,retain_private,reply_private,release_private,NULL};
#ifdef KERNEL_TEST_AP_CONSOLE
    config.step_limit = 65536;
#endif
    if (
#ifdef KERNEL_TEST_AP_CONCURRENT
        !x86_scheduler_dispatch_lot_on(cpu,xat_record.Root,1,&config,&local->stats) ||
#else
        !x86_scheduler_dispatch_retained_on(cpu,&config,&local->stats) ||
#endif
        local->current != KERNEL_SCHEDULER_NONE || local->generation ||
        kernel_scheduler_apply_on(cpu,KERNEL_SCHEDULER_CURRENT,KERNEL_SCHEDULER_NONE,0).status != KERNEL_SCHEDULER_EMPTY)
        xat_abort();
    record->Local = (uint64_t)(uintptr_t)local;
    record->Steps = local->stats.steps; record->Completed = local->stats.completed;
#ifdef KERNEL_TEST_AP_PREEMPTION
    if (!xat_preempt_quiet()) xat_abort();
#endif
#ifdef KERNEL_TEST_AP_CONCURRENT
#ifdef KERNEL_TEST_AP_NO_QUIESCE
    if (cpu != XAT_APS)
#endif
    if (kernel_scheduler_lot_on(cpu,KERNEL_LOT_QUIESCE,xat_record.Root,1,0).status) xat_abort();
#endif
    local->phase = 0;
}

// ====== xat_cleanup ======

void xat_cleanup(void)
{
#ifdef KERNEL_TEST_AP_CONSOLE
    if (!kca_release_allowed()) xat_abort();
#endif
    if (!x86_percpu_bsp() || xad_collect_allowed(1,0,1,xat_record.Root) != 1 ||
        !kernel_physical_budget_matches(xat_pool,xat_record.Held)) xat_abort();
    for (uint32_t index = 0; index < XAT_TASKS; ++index) {
        if (apply_private(0,KERNEL_SCHEDULER_QUERY,index).state != KERNEL_TASK_FINISHED ||
            !x86_task_stack_validate(&xat_stacks[index])) xat_abort();
    }
#ifdef KERNEL_TEST_AP_CONCURRENT
    for (uint32_t cpu = 2; cpu <= XAT_APS; ++cpu)
        if (xad_collect_allowed(cpu,0,1,xat_record.Root) != 1) xat_abort();
    if (kernel_scheduler_lot_on(0,KERNEL_LOT_QUERY,xat_record.Root,1,0).quiesced != XAT_PARTICIPANTS) xat_abort();
#endif
    xat_record.Retained = XAT_TASKS;
    for (uint32_t index = 0; index < XAT_TASKS; ++index) {
        if (!x86_task_stack_release(&xat_stacks[index],index,xat_record.Generations[index])) xat_abort();
        (void)apply_private(0,KERNEL_SCHEDULER_COLLECT,index);
        ++xat_record.Collected;
    }
#ifdef KERNEL_TEST_AP_CONCURRENT
    if (kernel_scheduler_lot_on(0,KERNEL_LOT_CLOSE,xat_record.Root,1,0).status) xat_abort();
#endif
    if (!kernel_physical_budget_read(xat_pool,&xat_record.Final) || xat_record.Final != xat_record.Budget ||
        kernel_scheduler_apply(KERNEL_SCHEDULER_SELECT,KERNEL_SCHEDULER_NONE,0).status != KERNEL_SCHEDULER_EMPTY) xat_abort();
    x86_scheduler_unbind();
    x86_scheduler_local_current()->phase = 3;
}
