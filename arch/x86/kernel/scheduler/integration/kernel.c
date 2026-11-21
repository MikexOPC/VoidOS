#include "control.h"
#include "local.h"
#include "arch/x86/configs/layout.h"
#include "../idle/idle.h"
#include "../persistent/persistent.h"
#include "../diagnostic/profiles.h"
#include "kernel/scheduler/kernel.h"
#include "kernel/scheduler/scheduler.h"
#include "kernel/sync/lock.h"
#include "kernel/sync/internal.h"
#include "kernel/diagnostics/log.h"
#include "arch/x86/kernel/cpu/percpu/resources/geometry.h"
#if KERNEL_TEST_AP_SERVICE == 9
#include "../batch/batch.h"
#endif
#ifdef KERNEL_AP_RUNTIME
#include "../remote/scope.h"
#endif
#ifdef KERNEL_AP_TASKS
#include "arch/x86/kernel/cpu/percpu/service/native/native.h"
#include "../remote/remote.h"
#endif
#ifdef KERNEL_PS2_INPUT
#include "arch/x86/kernel/drivers/input/ps2/runtime/session.h"
#include "../irq/preempt.h"
#endif

X86_TASK_STACK x86_task_stacks[8];
X86_TASK_CONTEXT x86_task_contexts[8];
uint32_t x86_scheduler_machine_mask;

static uint64_t budget;
static KERNEL_EVENT_REPLY event_replies[8];
static X86_TASK_ENTRY task_entries[8];
static uint32_t irq_profile;
static const X86_SCHEDULER_PROFILE *active_profile;
static KERNEL_PHYSICAL_ALLOCATOR *saved_pool;
static uint64_t saved_root;
#include "switch/bridge.h"

// ====== local_private ======

static X86_SCHEDULER_LOCAL *local_private(void)
{
    X86_SCHEDULER_LOCAL *local = x86_scheduler_local_current();
    if (!local) kernel_scheduler_fail_stop();
    return local;
}

// ====== cpu_private ======

static uint32_t cpu_private(void)
{
    const X86_CPU_LOCAL *owner = x86_percpu_current_local();
    return owner ? owner->Index : 0;
}

// ====== x86_scheduler_index ======

uint32_t x86_scheduler_index(void)
{
    X86_SCHEDULER_LOCAL *local = x86_scheduler_local_current();
    return local ? local->current : KERNEL_SCHEDULER_NONE;
}

// ====== x86_scheduler_generation ======

uint64_t x86_scheduler_generation(void)
{
    X86_SCHEDULER_LOCAL *local = x86_scheduler_local_current();
    return local ? local->generation : 0;
}

// ====== x86_scheduler_dispatcher_context_is ======

int x86_scheduler_dispatcher_context_is(const X86_TASK_CONTEXT *context)
{
    X86_SCHEDULER_LOCAL *local = x86_scheduler_local_current();
    if (!local) return 0;
    return context == &local->dispatcher && (local->phase == 1 || local->phase == 2);
}

// ====== kernel_scheduler_binding_context_valid ======

uint32_t kernel_scheduler_binding_context_valid(void)
{
    X86_SCHEDULER_LOCAL *local = x86_scheduler_local_current();
    if (!local) return 0;
    X86_TASK_CPU cpu = {0};
    if (local->current != KERNEL_SCHEDULER_NONE || !x86_main_pointer(x86_kernel_stack_pointer())) return 0;
    if (local->phase == 1) return x86_scheduler_native_gate();
    // Native task mapper is detached after collection. Revalidate CPU/stack,
    // using the established root and its still-owned physical allocator.
    return local->phase == 3 && saved_pool && kernel_physical_validate(saved_pool) &&
        x86_scheduler_cpu_capture(&cpu) && x86_scheduler_cpu_valid(&cpu,saved_root,cpu.apic_id) &&
        x86_scheduler_cpu_permissions() == 1 && x86_exception_environment() &&
        x86_kernel_stack_branch(saved_root,saved_pool->PhysicalBits);
}

// ====== dispatcher_resume_private ======

static int dispatcher_resume_private(void)
{
    X86_SCHEDULER_LOCAL *local = local_private();
    uint32_t cpu = cpu_private();
    uint64_t base = cpu ? X86_AP_STACK_BASE(cpu) : X86_KERNEL_MAIN_BASE;
    if (cpu >= 64 || !x86_scheduler_context_resume_valid(&local->dispatcher, base, 65536,
        X86_KERNEL_IMAGE_BASE, X86_KERNEL_IMAGE_LIMIT, x86_scheduler_machine_mask)) return 0;
    return x86_scheduler_code_valid(*(const uint64_t *)(uintptr_t)local->dispatcher.rsp);
}

// ====== kernel_scheduler_context_valid ======

uint32_t kernel_scheduler_context_valid(void)
{
#ifdef KERNEL_AP_RUNTIME
    if (x86_ap_actor()) return x86_ap_dispatcher_gate();
#endif
    X86_SCHEDULER_LOCAL *local = x86_scheduler_local_current();
    if (!local) return 0;
    // NMI/DF use IST. IRQ posts never borrow Rust state; mutations stay IF=0
    // on the dispatcher, in both the early and driver-owned sessions.
    return local->phase == 1 && x86_main_pointer(x86_kernel_stack_pointer()) && x86_scheduler_native_gate();
}

// ====== kernel_scheduler_fail_stop ======

__declspec(noreturn) void kernel_scheduler_fail_stop(void)
{
    x86_kernel_stop(UINT64_C(0x5343484641494C01));
}

// ====== apply_private ======

static KERNEL_SCHEDULER_REPLY apply_private(uint32_t operation, uint32_t index, uint64_t gen)
{
    KERNEL_SCHEDULER_REPLY reply = kernel_scheduler_apply(operation, index, gen);
    if (reply.status != KERNEL_SCHEDULER_OK) kernel_panic("SCHEDULER_RUST_STATE_INVALID");
    return reply;
}

// ====== resume_private ======

static int resume_private(uint32_t index)
{
    return x86_scheduler_resume_valid(&x86_task_contexts[index],index,x86_scheduler_machine_mask);
}

// ====== live_private ======

static int live_private(uint32_t index)
{
    X86_TASK_STACK *owner = &x86_task_stacks[index];
    return owner->stage == X86_TASK_STACK_READY && x86_task_stack_mapping_valid(owner,1) &&
        kernel_physical_is_pinned(owner->pool,&owner->lease) == 1;
}

// ====== x86_scheduler_binding_open ======

int x86_scheduler_binding_open(KERNEL_PHYSICAL_ALLOCATOR *pool, uint64_t root)
{
    __declspec(align(16)) unsigned char fx[512] = {0};
    if (!x86_scheduler_bind(x86_task_stacks,x86_task_contexts,pool,root)) return 0;
    if (!x86_scheduler_fp_template(fx)) { x86_scheduler_unbind(); return 0; }
    x86_scheduler_machine_mask = (uint32_t)fx[28] | ((uint32_t)fx[29] << 8) |
        ((uint32_t)fx[30] << 16) | ((uint32_t)fx[31] << 24);
    if (!x86_scheduler_machine_mask) x86_scheduler_machine_mask = 0xFFBF;
    for (uint32_t index = 0; index < X86_TASK_STACK_SLOTS; ++index) {
        *x86_scheduler_task_binding(index) = (X86_TASK_BINDING){0};
        task_entries[index] = NULL;
    }
    return 1;
}

// ====== x86_scheduler_task_assign ======

int x86_scheduler_task_assign(uint32_t index, uint32_t cpu, X86_TASK_ENTRY entry)
{
    if (index >= 8 || cpu >= 64 || !entry || !x86_scheduler_code_valid((uint64_t)(uintptr_t)entry) ||
        x86_task_stacks[index].self || !x86_percpu_bsp()) return 0;
    if (!x86_scheduler_task_binding_init(index,1ULL << cpu,cpu == 0)
    )
        return 0;

    task_entries[index] = entry;
    return 1;
}

// ====== x86_scheduler_task_assign_mask ======

int x86_scheduler_task_assign_mask(uint32_t index, uint64_t mask, X86_TASK_ENTRY entry) {
    if (index >= X86_TASK_STACK_SLOTS ||
        !entry ||
        !x86_scheduler_code_valid((uint64_t)(uintptr_t)entry) ||
        x86_task_stacks[index].self ||
        !x86_percpu_bsp() ||
        !x86_scheduler_task_binding_init(index,mask,0)
    )
        return 0;

    task_entries[index] = entry;
    return 1;
}

// ====== prepare_private ======

static void prepare_private(KERNEL_PHYSICAL_ALLOCATOR *pool, uint64_t root)
{
    KERNEL_SCHEDULER_REPLY reply = apply_private(KERNEL_SCHEDULER_RESERVE, KERNEL_SCHEDULER_NONE, 0);
    uint32_t index = reply.index;
    if (index >= 8 || reply.state != KERNEL_TASK_RESERVED || !reply.generation ||
        !x86_scheduler_task_assign(index,0,active_profile->entry) ||
        kernel_scheduler_affinity_on(0,index,reply.generation,1))
        kernel_panic("SCHEDULER_RESERVE_INVALID");
    if (!x86_task_stack_acquire(&x86_task_stacks[index], pool, root, index, reply.generation) ||
        !x86_scheduler_context_prepare(&x86_task_contexts[index], x86_task_stack_base(index), 65536) ||
        !resume_private(index)) {
        if (x86_task_stacks[index].self && !x86_task_stack_release(&x86_task_stacks[index], index, reply.generation))
            kernel_panic("SCHEDULER_ROLLBACK_FAILED");
        (void)apply_private(KERNEL_SCHEDULER_ABORT, index, reply.generation);
#if defined(KERNEL_TEST_SCHEDULER_FAULT) && KERNEL_TEST_SCHEDULER_FAULT == 5
        {
            static uint32_t retried;
            if (!retried && x86_scheduler_zero_fault_used() == 1 && kernel_physical_budget_matches(pool, budget)) {
                retried = 1;
                for (uint32_t slot = 320; slot < 464; ++slot)
                    if (x86_stack_entries(3)[slot]) kernel_panic("SCHEDULER_ROLLBACK_MAPPING_REMAINS");
                if (!kernel_log(0,"KERNEL_SCHEDULER_ROLLBACK_OK")) kernel_scheduler_fail_stop();
                prepare_private(pool, root); return; // Exactly one retry in isolated fault image.
            }
        }
#endif
        kernel_panic("SCHEDULER_PREPARE_FAILED");
    }
    reply = apply_private(KERNEL_SCHEDULER_PUBLISH, index, reply.generation);
    if (reply.state != KERNEL_TASK_READY) kernel_panic("SCHEDULER_PUBLISH_INVALID");
}

// ====== x86_scheduler_task_valid ======

int x86_scheduler_task_valid(void)
{
    X86_SCHEDULER_LOCAL *local = x86_scheduler_local_current();
    if (!local) return 0;
    uint64_t rsp = x86_kernel_stack_pointer(), base;
    uint32_t index = local->current;
    X86_TASK_BINDING *binding = x86_scheduler_task_binding(index);
    if (local->phase != 2 || !binding ||
        ks_atomic_load_acquire(&binding->Owner) != (uint64_t)cpu_private() + 1 ||
        x86_task_stacks[index].generation != local->generation || !x86_scheduler_native_gate()) return 0;
    base = x86_task_stack_base(index);
    return rsp >= base && rsp < base + 65536 && live_private(index);
}

// ====== x86_scheduler_driver_task_valid ======

int x86_scheduler_driver_task_valid(void)
{
    uint64_t saved_if = kernel_preempt_section_enter();
    X86_SCHEDULER_LOCAL *local = x86_scheduler_local_current();
    X86_TASK_BINDING *binding = local ? x86_scheduler_task_binding(local->current) : NULL;
    int valid = binding && binding->Driver && binding->Allowed == 1 &&
        !cpu_private() && x86_percpu_bsp() && x86_scheduler_task_valid();
    kernel_preempt_section_leave(saved_if);
    return valid;
}

// ====== x86_scheduler_yield ======

void x86_scheduler_yield(void)
{
    uint64_t saved_if = kernel_preempt_section_enter();
    X86_SCHEDULER_LOCAL *local = local_private();
    uint32_t index = local->current;
    uint64_t gen = local->generation;
    if (!x86_scheduler_task_valid() || local->event || !dispatcher_resume_private()) kernel_scheduler_fail_stop();
    local->event = 1;
    if (!switch_private(&x86_task_contexts[index], &local->dispatcher, UINT64_C(0x1001) + index))
        kernel_scheduler_fail_stop();
    local = local_private();
    if (!x86_scheduler_task_valid() || local->current != index || local->generation != gen || local->event)
        kernel_scheduler_fail_stop();
#ifdef KERNEL_PS2_INPUT
    if (!x86_preempt_rearm()
    )
        kernel_panic("PREEMPT_YIELD_REARM_FAILED");
#endif
    kernel_preempt_section_leave(saved_if);
}

// ====== suspend_private ======

static void suspend_private(uint32_t event)
{
    X86_SCHEDULER_LOCAL *local = local_private();
    uint32_t index = local->current;
    uint64_t gen = local->generation;
    if (!x86_scheduler_task_valid() || local->event || !dispatcher_resume_private()) kernel_scheduler_fail_stop();
    local->event = event;
    x86_scheduler_context_switch(&x86_task_contexts[index], &local->dispatcher);
    local = local_private();
    if (!x86_scheduler_task_valid() || local->current != index || local->generation != gen || local->event)
        kernel_scheduler_fail_stop();
}

// ====== x86_scheduler_irq_suspend ======

void x86_scheduler_irq_suspend(void)
{
    suspend_private(X86_TASK_PREEMPT);
}

// ====== x86_scheduler_tick_suspend ======

void x86_scheduler_tick_suspend(void)
{
    suspend_private(X86_TASK_TICK);
}

// ====== x86_scheduler_event_request ======

KERNEL_EVENT_REPLY x86_scheduler_event_request(uint32_t reason, uint32_t id, uint64_t token)
{
    uint64_t saved_if = kernel_preempt_section_enter();
    X86_SCHEDULER_LOCAL *local = local_private();
    uint32_t index = local->current;
    uint64_t gen = local->generation;
    if (!x86_scheduler_task_valid() || local->event || reason < 3 || reason > 7 || id >= 8 ||
        !dispatcher_resume_private()) kernel_scheduler_fail_stop();
    local->requested_event = id; local->requested_epoch = token; local->event = reason;
    if (!switch_private(&x86_task_contexts[index], &local->dispatcher, UINT64_C(0x3001) + index))
        kernel_scheduler_fail_stop();
    local = local_private();
    if (!x86_scheduler_task_valid() || local->current != index || local->generation != gen || local->event ||
        event_replies[index].reply.status != KERNEL_SCHEDULER_OK ||
        event_replies[index].reply.index != (reason >= 6 ? id : index) ||
        event_replies[index].reply.generation != (reason >= 6 ? token : gen)) kernel_scheduler_fail_stop();
    KERNEL_EVENT_REPLY reply = event_replies[index];
#ifdef KERNEL_PS2_INPUT
    if (!x86_preempt_rearm()
    )
        kernel_panic("PREEMPT_EVENT_REARM_FAILED");
#endif
    kernel_preempt_section_leave(saved_if);
    return reply;
}

// ====== x86_scheduler_task_start ======

void x86_scheduler_task_start(void)
{
    X86_SCHEDULER_LOCAL *local = local_private();
    X86_TASK_ENTRY entry = local->current < 8 ? task_entries[local->current] : NULL;
    if (!x86_scheduler_task_valid() || local->event || !entry || !x86_scheduler_code_valid((uint64_t)(uintptr_t)entry))
        kernel_scheduler_fail_stop();
#if defined(KERNEL_PS2_INPUT) && defined(KERNEL_USER_PROCESS)
    if (irq_profile == 5
    ) {
        if (!x86_preempt_rearm()
        )
            kernel_panic("PREEMPT_START_REARM_FAILED");

        kernel_preempt_section_leave(0x200);
    }
#endif
    entry(local->current);
#if !defined(KERNEL_TEST_SCHEDULER_FAULT) || KERNEL_TEST_SCHEDULER_FAULT != 6
    x86_scheduler_exit();
#endif
}

// ====== x86_scheduler_exit ======

__declspec(noreturn) void x86_scheduler_exit(void)
{
    (void)kernel_preempt_section_enter();
    X86_SCHEDULER_LOCAL *local = local_private();
    if (!x86_scheduler_task_valid() || local->event || !dispatcher_resume_private()) kernel_scheduler_fail_stop();
    local->event = X86_TASK_EXIT;
    x86_scheduler_context_switch(&x86_task_contexts[local->current], &local->dispatcher);
    kernel_scheduler_fail_stop();
}

// ====== x86_scheduler_dispatch_run ======

int x86_scheduler_dispatch_run(const KERNEL_SCHEDULER_REPLY *task, X86_DISPATCH_COMMAND *command)
{
    X86_SCHEDULER_LOCAL *local = local_private();
    uint32_t cpu = cpu_private();
    if (!task || !command || local->current != KERNEL_SCHEDULER_NONE || !kernel_scheduler_context_valid() || task->index >= 8 ||
        x86_task_stacks[task->index].generation != task->generation) return 0;
    const X86_CPU_LOCAL *owner = x86_percpu_current_local();
    if (!owner ||
        !x86_scheduler_task_claim(task->index,cpu,owner->Locks.Wait) ||
        !resume_private(task->index)
    )
        return 0;
    local->current = task->index; local->generation = task->generation; local->event = 0;
    local->phase = 2;
    if (!switch_private(&local->dispatcher,&x86_task_contexts[local->current],UINT64_C(0x2003))) kernel_scheduler_fail_stop();
    local->phase = 1;
    if (!kernel_scheduler_context_valid() ||
        !(cpu ? live_private(local->current) : x86_task_stack_validate(&x86_task_stacks[local->current]))) return 0;
    command->reason = local->event;
    command->target = local->event >= X86_TASK_OBSERVE && local->event <= X86_TASK_STOP_QUERY ? local->requested_event : 0;
    command->token = local->event >= X86_TASK_OBSERVE && local->event <= X86_TASK_STOP_QUERY ? local->requested_epoch : 0;
    return 1;
}

// ====== native_dispatch_collect_private ======

static int native_dispatch_collect_private(const KERNEL_SCHEDULER_REPLY *task)
{
    X86_SCHEDULER_LOCAL *local = local_private();
#ifdef KERNEL_AP_TASKS
    if (x86_remote_dispatch_collect(task))
        return X86_DISPATCH_COLLECT_DEFERRED;
#endif
#ifdef KERNEL_PS2_INPUT
    if (irq_profile == 5 && !x86_ps2_before_collect()) return 0;
#endif
    return task && local->current < 8 && task->index == local->current && task->generation == local->generation &&
        kernel_scheduler_context_valid() && x86_task_stack_release(&x86_task_stacks[local->current],local->current,local->generation);
}

// ====== x86_scheduler_dispatch_reply ======

int x86_scheduler_dispatch_reply(const KERNEL_SCHEDULER_REPLY *task, const KERNEL_EVENT_REPLY *reply)
{
    X86_SCHEDULER_LOCAL *local = local_private();
    if (!task || !reply || task->index != local->current || task->generation != local->generation ||
        local->current >= 8 || !kernel_scheduler_context_valid()) return 0;
    event_replies[local->current] = *reply;
    return 1;
}

// ====== x86_scheduler_dispatch_release ======

int x86_scheduler_dispatch_release(void)
{
    X86_SCHEDULER_LOCAL *local = local_private();
    if (!kernel_scheduler_context_valid()) return 0;
    uint32_t index = local->current, reason = local->event;
    uint64_t generation = local->generation;
    local->current = KERNEL_SCHEDULER_NONE; local->generation = 0;
    local->event = 0;
#ifdef KERNEL_PS2_INPUT
    if (reason == X86_TASK_TICK && !x86_preempt_rearm()
    )
        return 0;
#endif
    if (!x86_scheduler_task_release(index,cpu_private())
    )
        return 0;
#ifdef KERNEL_AP_TASKS
    x86_remote_depart(index,generation,reason);
#else
    (void)generation;
    (void)reason;
#endif
#ifdef KERNEL_PS2_INPUT
    if (x86_percpu_bsp() && irq_profile == 5 && x86_ps2_session()->Active && !x86_ps2_after_dispatch()) return 0;
#endif
    return 1;
}

// ====== session_complete_private ======

static int session_complete_private(void)
{
    X86_SCHEDULER_LOCAL *local = local_private();
    KERNEL_SCHEDULER_REPLY reply = kernel_scheduler_apply(KERNEL_SCHEDULER_SELECT,KERNEL_SCHEDULER_NONE,0);
    if (reply.status != KERNEL_SCHEDULER_EMPTY || !x86_scheduler_profile_complete(irq_profile,&local->stats) ||
        !kernel_physical_validate(saved_pool) || !kernel_physical_budget_matches(saved_pool,budget)) return 0;
    for (uint32_t index = 0; index < X86_TASK_STACK_SLOTS; ++index) {
        if (x86_task_stacks[index].self) return 0;
    }
    for (uint32_t index = 320; index < 464; ++index) {
        if (x86_stack_entries(3)[index]) return 0;
    }
    return x86_scheduler_report(saved_root,budget,saved_pool,local->stats.steps,irq_profile) &&
        kernel_log(0,"KERNEL_SCHEDULER_WAIT_OK") && kernel_log(0,"KERNEL_SCHEDULER_SWITCH_OK") &&
        kernel_log(0,"KERNEL_SCHEDULER_CLEANUP_OK");
}

#ifdef KERNEL_PS2_INPUT
// ====== runtime_complete_private ======

static int runtime_complete_private(void)
{
    if (x86_ps2_session()->Active) {
        if (!session_complete_private() || !x86_ps2_session_finish()) return 0;
    }
    else if (!x86_preempt_pause()) return 0;

    if (!x86_watchdog_valid() || !x86_preempt_report() || !x86_preempt_resume()) return 0;
    return kernel_log(0,"KERNEL_READY") && kernel_log(0,"KERNEL_DISPATCHER_READY");
}

// ====== runtime_idle_private ======

static int runtime_idle_private(void)
{
    _Alignas(8) volatile BOOT_U32 source[2] = {0,0};
    X86_SCHEDULER_LOCAL *local = local_private();
    BOOT_U64 pages;
    if (!x86_watchdog_idle(source) || !kernel_physical_budget_matches(saved_pool,budget) ||
        !kernel_physical_budget_read(saved_pool,&pages) || local->idle_cycles == UINT64_MAX) return 0;
    ++local->idle_cycles;
    if (local->idle_cycles != 1 && local->idle_cycles % 200) return 1;
    return kernel_text("KERNEL_DISPATCH_IDLE_CYCLES: ") && kernel_number(local->idle_cycles) &&
        kernel_text("\nKERNEL_DISPATCH_TIMER_TICKS: ") && kernel_number(x86_preempt_ticks()) &&
        kernel_text("\nKERNEL_DISPATCH_FREE_PAGES: ") && kernel_number(pages) && kernel_text("\n") &&
        kernel_log(0,"KERNEL_DISPATCH_IDLE_OK");
}
#endif

// ====== kernel_scheduler_run ======

int kernel_scheduler_run(KERNEL_PHYSICAL_ALLOCATOR *pool, BOOT_U64 root)
{
    if (!x86_percpu_bsp()) return 0;
    X86_SCHEDULER_LOCAL *local = x86_scheduler_local_prepare();
    if (!local) return 0;
    X86_DISPATCH_CONFIG config;
    uint32_t task_count;
    int continuous = 0;
    active_profile = x86_scheduler_profile(irq_profile);
    if (local->phase || !pool || !active_profile || !kernel_section("scheduler cooperativo BSP") ||
        !x86_scheduler_binding_open(pool, root)) return 0;
    local->phase = 1; if (!kernel_physical_budget_read(pool, &budget)) return 0;
    task_count = active_profile->task_count;
#ifdef KERNEL_PS2_INPUT
    continuous = irq_profile == 5 && x86_ps2_session()->Persistent;
    if (continuous && !x86_ps2_session()->Active) task_count = 0;
#endif
    if (!irq_profile) {
        (void)apply_private(KERNEL_SCHEDULER_INIT, KERNEL_SCHEDULER_NONE, 0);
        saved_pool = pool; saved_root = root;
    }
#ifdef KERNEL_AP_TASKS
    if (irq_profile == 5 && xis_native.Count >= 2 && kernel_scheduler_configure(xis_native.Count,1)) return 0;
    if (irq_profile == 5 && !x86_ap_dispatchers_start(pool,root)) return 0;
#endif
    for (uint32_t index = 0; index < task_count; ++index) prepare_private(pool,root);
    if ((irq_profile == 2 || irq_profile == 4
#ifdef KERNEL_PS2_INPUT
        || (irq_profile == 5 && task_count)
#endif
        ) &&
        !x86_watchdog_budget_begin_four(&x86_task_stacks[0].lease,&x86_task_stacks[1].lease,irq_profile == 5 ? &x86_task_stacks[2].lease : NULL,
            task_count == 4 ? &x86_task_stacks[3].lease : NULL))
        kernel_panic("SCHEDULER_IDLE_ACCOUNTING_FAILED");
#ifdef KERNEL_PS2_INPUT
    if (continuous && !x86_preempt_open(x86_timer_owner())) return 0;
    if (irq_profile == 5 && task_count && !x86_ps2_activate()) return 0;
#endif
    if (!kernel_log(0, "KERNEL_SCHEDULER_STACKS_OK") ||
        !kernel_text("SCHEDULER_CONTEXTS: ") || !kernel_number((BOOT_U64)x86_task_contexts) || !kernel_text("\n")) return 0;
    if (!kernel_text("SCHEDULER_ROOT: ") || !kernel_number(root) ||
        !kernel_text("\nSCHEDULER_PTE: ") || !kernel_number((BOOT_U64)&x86_stack_entries(3)[320]) || !kernel_text("\n")) return 0;
#if defined(KERNEL_TEST_SCHEDULER_FAULT) && KERNEL_TEST_SCHEDULER_FAULT < 5
    x86_scheduler_test_fault(1);
#endif
    config = (X86_DISPATCH_CONFIG){task_count,active_profile->step_limit,
        active_profile->idle_limit,active_profile->drain_irq,x86_scheduler_dispatch_run,
        native_dispatch_collect_private,x86_scheduler_dispatch_reply,x86_scheduler_dispatch_release,active_profile->idle};
#ifdef KERNEL_PS2_INPUT
    if (continuous) {
        (void)x86_scheduler_dispatch_continuous(&config,&local->stats,runtime_complete_private,runtime_idle_private);
        kernel_text("SCHEDULER_DISPATCH_FAILURE: ");
        kernel_number(local->stats.failure);
        kernel_text("\n");
        kernel_panic("SCHEDULER_RUNTIME_FAILED");
    }
#else
    (void)continuous;
#endif
    if (!x86_scheduler_dispatch(&config,&local->stats)) {
        kernel_text("SCHEDULER_DISPATCH_FAILURE: "); kernel_number(local->stats.failure); kernel_text("\n");
#if defined(KERNEL_TEST_SCHEDULER_FAULT) && KERNEL_TEST_SCHEDULER_FAULT == 8
        if (irq_profile == 2 && local->stats.failure == 2 && x86_watchdog_result() == X86_WATCHDOG_TIMEOUT &&
            x86_watchdog_failure() == X86_WATCHDOG_DEADLINE && x86_scheduler_idle_retained_valid() &&
            kernel_log(2,"KERNEL_SCHEDULER_IDLE_TIMEOUT_OK")) kernel_scheduler_fail_stop();
#elif defined(KERNEL_TEST_SCHEDULER_FAULT) && KERNEL_TEST_SCHEDULER_FAULT == 9
        if (irq_profile == 4 && local->stats.failure == 2 && x86_watchdog_result() == X86_WATCHDOG_TIMEOUT &&
            x86_watchdog_failure() == X86_WATCHDOG_DEADLINE && x86_scheduler_idle_retained_valid() &&
            kernel_log(2,"KERNEL_SCHEDULER_IDLE_REPEAT_TIMEOUT_OK")) kernel_scheduler_fail_stop();
#endif
        kernel_panic("SCHEDULER_DISPATCH_FAILED");
    }
    if (!session_complete_private()) return 0;
#if defined(KERNEL_TEST_SCHEDULER_FAULT) && KERNEL_TEST_SCHEDULER_FAULT < 5
    x86_scheduler_test_fault(4);
#endif
    local->phase = 3; x86_scheduler_unbind();
    return kernel_log(0, "KERNEL_SCHEDULER_OK");
}

#ifdef KERNEL_PS2_INPUT
// ====== kernel_scheduler_prepare ======

int kernel_scheduler_prepare(KERNEL_PHYSICAL_ALLOCATOR *pool, BOOT_U64 root)
{
    if (!x86_percpu_bsp() || !pool || irq_profile || saved_pool) return 0;
    X86_SCHEDULER_LOCAL *local = x86_scheduler_local_prepare();
    if (!local || local->phase || local->current != KERNEL_SCHEDULER_NONE ||
        !x86_scheduler_binding_open(pool, root)) return 0;
    local->phase = 1;
    (void)apply_private(KERNEL_SCHEDULER_INIT, KERNEL_SCHEDULER_NONE, 0);
    saved_pool = pool;
    saved_root = root;
    irq_profile = 4;
    local->phase = 3;
    x86_scheduler_unbind();
    return kernel_log(0, "KERNEL_SCHEDULER_READY");
}
#endif

// ====== next_session_private ======

static int next_session_private(uint32_t previous)
{
    X86_SCHEDULER_LOCAL *local = x86_scheduler_local_current();
    if (!local) return 0;
    if (local->phase != 3 || irq_profile != previous || previous >= 5 || !saved_pool || local->current != KERNEL_SCHEDULER_NONE) return 0;
    irq_profile = previous + 1; local->phase = 0; local->stats = (X86_DISPATCH_STATS){0};
    return kernel_scheduler_run(saved_pool, saved_root);
}

#ifdef KERNEL_PS2_INPUT
// ====== x86_scheduler_run_input ======

int x86_scheduler_run_input(void) { return next_session_private(4); }
#endif

#ifndef KERNEL_PS2_INPUT
// ====== x86_scheduler_run_irq ======

int x86_scheduler_run_irq(void)
{
    return next_session_private(0);
}

// ====== x86_scheduler_run_idle ======

int x86_scheduler_run_idle(void)
{
    return next_session_private(1);
}

// ====== x86_scheduler_run_persistent ======

int x86_scheduler_run_persistent(void)
{
    return next_session_private(2);
}

#endif

#ifdef KERNEL_TEST_SCHEDULER_PERIODIC
// ====== x86_scheduler_run_periodic ======

int x86_scheduler_run_periodic(void) { return next_session_private(4); }
#endif

#ifndef KERNEL_PS2_INPUT
// ====== x86_scheduler_idle_retained_valid ======

int x86_scheduler_idle_retained_valid(void)
{
    X86_SCHEDULER_LOCAL *local = local_private();
    return (irq_profile == 2 || irq_profile == 4) && local->stats.waiting_count == 2 && x86_scheduler_retained_valid();
}

#endif

// ====== x86_scheduler_retained_valid ======

int x86_scheduler_retained_valid(void)
{
    X86_SCHEDULER_LOCAL *local = x86_scheduler_local_current();
    if (!local) return 0;
    if (local->phase != 1 || local->current != KERNEL_SCHEDULER_NONE || local->stats.completed || !active_profile ||
        !kernel_scheduler_context_valid() || !kernel_physical_validate(saved_pool) ||
        active_profile->task_count < 2 || active_profile->task_count > 8 ||
        budget < 16 * active_profile->task_count) return 0;
    uint32_t live = active_profile->task_count;
    for (uint32_t index = 0; index < 8; ++index) {
        if (index >= active_profile->task_count && !x86_task_stacks[index].self) continue;
        if (!x86_task_stack_validate(&x86_task_stacks[index])) return 0;
        if (index >= active_profile->task_count) ++live;
    }
    return budget >= 16 * live && kernel_physical_budget_matches(saved_pool, budget - 16 * live);
}

// ====== x86_scheduler_session_stats ======

const X86_DISPATCH_STATS *x86_scheduler_session_stats(void)
{
    X86_SCHEDULER_LOCAL *local = x86_scheduler_local_current();
    return local ? &local->stats : NULL;
}

#ifndef KERNEL_PS2_INPUT
// ====== x86_scheduler_run_idle_repeat ======

int x86_scheduler_run_idle_repeat(void)
{
    return next_session_private(3);
}

#endif

// ====== x86_scheduler_irq_timeout_valid ======

int x86_scheduler_irq_timeout_valid(void)
{
    X86_SCHEDULER_LOCAL *local = x86_scheduler_local_current();
    if (!local) return 0;
    if (!irq_profile || local->phase != 2 || local->current != 1 || local->stats.waiting_count != 1 || local->stats.completed ||
        !x86_scheduler_task_valid() || !kernel_physical_validate(saved_pool) ||
        !kernel_physical_budget_matches(saved_pool, budget - 32)) return 0;
    // Validate retained leases from a task without invoking dispatcher-only APIs.
    for (uint32_t index = 0; index < 2; ++index) {
        if (x86_task_stacks[index].self != (uint64_t)(uintptr_t)&x86_task_stacks[index] || x86_task_stacks[index].index != index ||
            x86_task_stacks[index].pool != saved_pool || x86_task_stacks[index].root != saved_root ||
            !x86_task_stacks[index].generation || x86_task_stacks[index].stage != X86_TASK_STACK_READY ||
            !x86_task_stack_mapping_valid(&x86_task_stacks[index],1) ||
            kernel_physical_is_pinned(saved_pool,&x86_task_stacks[index].lease) != 1) return 0;
    }
    return 1;
}

// ====== x86_scheduler_irq_stack_top ======

uint64_t x86_scheduler_irq_stack_top(uint64_t frame)
{
    X86_SCHEDULER_LOCAL *local = x86_scheduler_local_current();
    if (!local) return 0;
    uint64_t base;
    if (!irq_profile || !x86_scheduler_task_valid() || local->current >= 8 || (frame & 7)) return 0;
    base = x86_task_stack_base(local->current);
    return frame >= base && frame <= base + 65536 - 48 ? base + 65536 : 0;
}

#if KERNEL_TEST_AP_SERVICE == 9 || defined(KERNEL_AP_TASKS)
// ====== x86_scheduler_batch_resources ======

KERNEL_PHYSICAL_ALLOCATOR *x86_scheduler_batch_resources(uint64_t *root)
{
    X86_SCHEDULER_LOCAL *local = x86_scheduler_local_current();
    if (!root || !x86_percpu_bsp() || !local || local->phase != 3 ||
        local->current != KERNEL_SCHEDULER_NONE || !saved_pool || !kernel_physical_validate(saved_pool)) return NULL;
    *root = saved_root;
    return saved_pool;
}
#endif
