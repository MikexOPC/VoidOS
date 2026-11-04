#include "control.h"
#include "arch/x86/configs/layout.h"
#include "../idle/idle.h"
#include "../persistent/persistent.h"
#include "../diagnostic/profiles.h"
#include "kernel/scheduler/kernel.h"
#include "kernel/scheduler/scheduler.h"
#include "kernel/diagnostics/log.h"
#ifdef KERNEL_PS2_INPUT
#include "arch/x86/kernel/drivers/input/ps2/runtime/session.h"
#endif

static X86_TASK_STACK stacks[8];
static X86_TASK_CONTEXT contexts[8], dispatcher;
static uint32_t phase, current = KERNEL_SCHEDULER_NONE, event;

static uint64_t generation, budget;
static uint32_t x86_scheduler_machine_mask;
static X86_DISPATCH_STATS stats;
static uint32_t requested_event;
static uint64_t requested_epoch;
static KERNEL_EVENT_REPLY event_replies[8];
static uint32_t irq_profile;
static const X86_SCHEDULER_PROFILE *active_profile;
static KERNEL_PHYSICAL_ALLOCATOR *saved_pool;
static uint64_t saved_root;
#include "switch/bridge.h"

// ====== x86_scheduler_index ======

uint32_t x86_scheduler_index(void) { return current; }

// ====== x86_scheduler_generation ======

uint64_t x86_scheduler_generation(void) { return generation; }

// ====== x86_scheduler_dispatcher_context_is ======

int x86_scheduler_dispatcher_context_is(const X86_TASK_CONTEXT *context)
{
    return context == &dispatcher && (phase == 1 || phase == 2);
}

// ====== kernel_scheduler_binding_context_valid ======

uint32_t kernel_scheduler_binding_context_valid(void)
{
    X86_TASK_CPU cpu = {0};
    if (current != KERNEL_SCHEDULER_NONE || !x86_main_pointer(x86_kernel_stack_pointer())) return 0;
    if (phase == 1) return x86_scheduler_native_gate();
    // Native task mapper is detached after collection. Revalidate CPU/stack,
    // using the established root and its still-owned physical allocator.
    return phase == 3 && saved_pool && kernel_physical_validate(saved_pool) &&
        x86_scheduler_cpu_capture(&cpu) && x86_scheduler_cpu_valid(&cpu,saved_root,cpu.apic_id) &&
        x86_scheduler_cpu_permissions() == 1 && x86_exception_environment() &&
        x86_kernel_stack_branch(saved_root,saved_pool->PhysicalBits);
}

// ====== dispatcher_resume_private ======

static int dispatcher_resume_private(void)
{
    if (!x86_scheduler_context_resume_valid(&dispatcher, X86_KERNEL_MAIN_BASE, 65536,
        X86_KERNEL_IMAGE_BASE, X86_KERNEL_IMAGE_LIMIT, x86_scheduler_machine_mask)) return 0;
    return x86_scheduler_code_valid(*(const uint64_t *)(uintptr_t)dispatcher.rsp);
}

// ====== kernel_scheduler_context_valid ======

uint32_t kernel_scheduler_context_valid(void)
{
    // NMI/DF use IST. IRQ posts never borrow Rust state; mutations stay IF=0
    // on the dispatcher, in both the early and driver-owned sessions.
    return phase == 1 && x86_main_pointer(x86_kernel_stack_pointer()) && x86_scheduler_native_gate();
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

static int resume_private(uint32_t index) { return x86_scheduler_resume_valid(&contexts[index],index,x86_scheduler_machine_mask); }


// ====== prepare_private ======

static void prepare_private(KERNEL_PHYSICAL_ALLOCATOR *pool, uint64_t root)
{
    KERNEL_SCHEDULER_REPLY reply = apply_private(KERNEL_SCHEDULER_RESERVE, KERNEL_SCHEDULER_NONE, 0);
    uint32_t index = reply.index;
    if (index >= 8 || reply.state != KERNEL_TASK_RESERVED || !reply.generation)
        kernel_panic("SCHEDULER_RESERVE_INVALID");
    if (!x86_task_stack_acquire(&stacks[index], pool, root, index, reply.generation) ||
        !x86_scheduler_context_prepare(&contexts[index], x86_task_stack_base(index), 65536) ||
        !resume_private(index)) {
        if (stacks[index].self && !x86_task_stack_release(&stacks[index], index, reply.generation))
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
    uint64_t rsp = x86_kernel_stack_pointer(), base;
    if (phase != 2 || current >= 8 || stacks[current].generation != generation ||
        stacks[current].stage != X86_TASK_STACK_READY || !x86_scheduler_native_gate()) return 0;
    base = x86_task_stack_base(current);
    return rsp >= base && rsp < base + 65536 &&
        x86_task_stack_mapping_valid(&stacks[current], 1) &&
        kernel_physical_is_pinned(stacks[current].pool, &stacks[current].lease) == 1;
}

// ====== x86_scheduler_yield ======

void x86_scheduler_yield(void)
{
    uint32_t index = current;
    uint64_t gen = generation;
    if (!x86_scheduler_task_valid() || event || !dispatcher_resume_private()) kernel_scheduler_fail_stop();
    event = 1;
    if (!switch_private(&contexts[index], &dispatcher, UINT64_C(0x1001) + index))
        kernel_scheduler_fail_stop();
    if (!x86_scheduler_task_valid() || current != index || generation != gen || event)
        kernel_scheduler_fail_stop();
}

#ifdef KERNEL_TEST_SCHEDULER_PREEMPTION
// ====== x86_scheduler_irq_suspend ======

void x86_scheduler_irq_suspend(void)
{
    uint32_t index = current;
    uint64_t gen = generation;
    if (!x86_scheduler_task_valid() || event || !dispatcher_resume_private()) kernel_scheduler_fail_stop();
    event = X86_TASK_PREEMPT;
    x86_scheduler_context_switch(&contexts[index], &dispatcher);
    if (!x86_scheduler_task_valid() || current != index || generation != gen || event)
        kernel_scheduler_fail_stop();
}
#endif

// ====== x86_scheduler_event_request ======

KERNEL_EVENT_REPLY x86_scheduler_event_request(uint32_t reason, uint32_t id, uint64_t token)
{
    uint32_t index = current;
    uint64_t gen = generation;
    if (!x86_scheduler_task_valid() || event || reason < 3 || reason > 7 || id >= 8 ||
        !dispatcher_resume_private()) kernel_scheduler_fail_stop();
    requested_event = id; requested_epoch = token; event = reason;
    if (!switch_private(&contexts[index], &dispatcher, UINT64_C(0x3001) + index) ||
        !x86_scheduler_task_valid() || current != index || generation != gen || event ||
        event_replies[index].reply.status != KERNEL_SCHEDULER_OK ||
        event_replies[index].reply.index != (reason >= 6 ? id : index) ||
        event_replies[index].reply.generation != (reason >= 6 ? token : gen)) kernel_scheduler_fail_stop();
    return event_replies[index];
}

// ====== x86_scheduler_task_start ======

void x86_scheduler_task_start(void)
{
    uint64_t entry = active_profile ? (uint64_t)(uintptr_t)active_profile->entry : 0;
    if (!x86_scheduler_task_valid() || event || !x86_scheduler_code_valid(entry))
        kernel_scheduler_fail_stop();
    active_profile->entry(current);
#if !defined(KERNEL_TEST_SCHEDULER_FAULT) || KERNEL_TEST_SCHEDULER_FAULT != 6
    if (!x86_scheduler_task_valid() || event || !dispatcher_resume_private()) kernel_scheduler_fail_stop();
    event = 2;
    x86_scheduler_context_switch(&contexts[current], &dispatcher);
    kernel_scheduler_fail_stop();
#endif
}

// ====== native_dispatch_run_private ======

static int native_dispatch_run_private(const KERNEL_SCHEDULER_REPLY *task, X86_DISPATCH_COMMAND *command)
{
    if (!task || !command || current != KERNEL_SCHEDULER_NONE || !kernel_scheduler_context_valid() || task->index >= 8 ||
        stacks[task->index].generation != task->generation || !resume_private(task->index)) return 0;
    current = task->index; generation = task->generation; event = 0;
    phase = 2;
    if (!switch_private(&dispatcher,&contexts[current],UINT64_C(0x2003))) kernel_scheduler_fail_stop();
    phase = 1;
    if (!kernel_scheduler_context_valid() || !x86_task_stack_validate(&stacks[current])) return 0;
    command->reason = event;
    command->target = event >= X86_TASK_OBSERVE && event <= X86_TASK_STOP_QUERY ? requested_event : 0;
    command->token = event >= X86_TASK_OBSERVE && event <= X86_TASK_STOP_QUERY ? requested_epoch : 0;
    event = 0;
    return 1;
}

// ====== native_dispatch_collect_private ======

static int native_dispatch_collect_private(const KERNEL_SCHEDULER_REPLY *task)
{
#ifdef KERNEL_PS2_INPUT
    if (irq_profile == 5 && !x86_ps2_before_collect()) return 0;
#endif
    return task && current < 8 && task->index == current && task->generation == generation &&
        kernel_scheduler_context_valid() && x86_task_stack_release(&stacks[current],current,generation);
}

// ====== native_dispatch_reply_private ======

static int native_dispatch_reply_private(const KERNEL_SCHEDULER_REPLY *task, const KERNEL_EVENT_REPLY *reply)
{
    if (!task || !reply || task->index != current || task->generation != generation ||
        current >= 8 || !kernel_scheduler_context_valid()) return 0;
    event_replies[current] = *reply;
    return 1;
}

// ====== native_dispatch_release_private ======

static int native_dispatch_release_private(void)
{
    if (!kernel_scheduler_context_valid()) return 0;
    current = KERNEL_SCHEDULER_NONE; generation = 0;
#ifdef KERNEL_PS2_INPUT
    if (irq_profile == 5 && !x86_ps2_after_dispatch()) return 0;
#endif
    return 1;
}

// ====== kernel_scheduler_run ======

int kernel_scheduler_run(KERNEL_PHYSICAL_ALLOCATOR *pool, BOOT_U64 root)
{
    __declspec(align(16)) unsigned char fx[512] = {0};
    KERNEL_SCHEDULER_REPLY reply;
    X86_DISPATCH_CONFIG config;
    active_profile = x86_scheduler_profile(irq_profile);
    if (phase || !pool || !active_profile || !kernel_section("scheduler cooperativo BSP") ||
        !x86_scheduler_bind(stacks, contexts, pool, root)) return 0;
    phase = 1; if (!kernel_physical_budget_read(pool, &budget)) return 0;
    if (!x86_scheduler_fp_template(fx)) return 0;
    x86_scheduler_machine_mask = (uint32_t)fx[28] | ((uint32_t)fx[29] << 8) |
        ((uint32_t)fx[30] << 16) | ((uint32_t)fx[31] << 24);
    if (!x86_scheduler_machine_mask) x86_scheduler_machine_mask = 0xFFBF;
    if (!irq_profile) {
        (void)apply_private(KERNEL_SCHEDULER_INIT, KERNEL_SCHEDULER_NONE, 0);
        saved_pool = pool; saved_root = root;
    }
    for (uint32_t index = 0; index < active_profile->task_count; ++index) prepare_private(pool,root);
    if ((irq_profile == 2 || irq_profile == 4 || irq_profile == 5) &&
        !x86_watchdog_budget_begin_four(&stacks[0].lease,&stacks[1].lease,irq_profile == 5 ? &stacks[2].lease : NULL,
            active_profile->task_count == 4 ? &stacks[3].lease : NULL))
        kernel_panic("SCHEDULER_IDLE_ACCOUNTING_FAILED");
#ifdef KERNEL_PS2_INPUT
    if (irq_profile == 5 && !x86_ps2_activate()) return 0;
#endif
    if (!kernel_log(0, "KERNEL_SCHEDULER_STACKS_OK") ||
        !kernel_text("SCHEDULER_CONTEXTS: ") || !kernel_number((BOOT_U64)contexts) || !kernel_text("\n")) return 0;
    if (!kernel_text("SCHEDULER_ROOT: ") || !kernel_number(root) ||
        !kernel_text("\nSCHEDULER_PTE: ") || !kernel_number((BOOT_U64)&x86_stack_entries(3)[320]) || !kernel_text("\n")) return 0;
#if defined(KERNEL_TEST_SCHEDULER_FAULT) && KERNEL_TEST_SCHEDULER_FAULT < 5
    x86_scheduler_test_fault(1);
#endif
    config = (X86_DISPATCH_CONFIG){active_profile->task_count,active_profile->step_limit,
        active_profile->idle_limit,active_profile->drain_irq,native_dispatch_run_private,
        native_dispatch_collect_private,native_dispatch_reply_private,native_dispatch_release_private,active_profile->idle};
    if (!x86_scheduler_dispatch(&config,&stats)) {
        kernel_text("SCHEDULER_DISPATCH_FAILURE: "); kernel_number(stats.failure); kernel_text("\n");
#if defined(KERNEL_TEST_SCHEDULER_FAULT) && KERNEL_TEST_SCHEDULER_FAULT == 8
        if (irq_profile == 2 && stats.failure == 2 && x86_watchdog_result() == X86_WATCHDOG_TIMEOUT &&
            x86_watchdog_failure() == X86_WATCHDOG_DEADLINE && x86_scheduler_idle_retained_valid() &&
            kernel_log(2,"KERNEL_SCHEDULER_IDLE_TIMEOUT_OK")) kernel_scheduler_fail_stop();
#elif defined(KERNEL_TEST_SCHEDULER_FAULT) && KERNEL_TEST_SCHEDULER_FAULT == 9
        if (irq_profile == 4 && stats.failure == 2 && x86_watchdog_result() == X86_WATCHDOG_TIMEOUT &&
            x86_watchdog_failure() == X86_WATCHDOG_DEADLINE && x86_scheduler_idle_retained_valid() &&
            kernel_log(2,"KERNEL_SCHEDULER_IDLE_REPEAT_TIMEOUT_OK")) kernel_scheduler_fail_stop();
#endif
        kernel_panic("SCHEDULER_DISPATCH_FAILED");
    }
    reply = kernel_scheduler_apply(KERNEL_SCHEDULER_SELECT, KERNEL_SCHEDULER_NONE, 0);
    if (reply.status != KERNEL_SCHEDULER_EMPTY || !x86_scheduler_profile_complete(irq_profile,&stats) || !kernel_physical_validate(pool) ||
        !kernel_physical_budget_matches(pool, budget)) return 0;
    for (uint32_t index = 0; index < 8; ++index) if (stacks[index].self) return 0;
    for (uint32_t index = 320; index < 464; ++index) if (x86_stack_entries(3)[index]) return 0;
    if (!x86_scheduler_report(root,budget,pool,stats.steps,irq_profile)) return 0;
    if (!kernel_log(0, "KERNEL_SCHEDULER_WAIT_OK") || !kernel_log(0, "KERNEL_SCHEDULER_SWITCH_OK") ||
        !kernel_log(0, "KERNEL_SCHEDULER_CLEANUP_OK")) return 0;
#if defined(KERNEL_TEST_SCHEDULER_FAULT) && KERNEL_TEST_SCHEDULER_FAULT < 5
    x86_scheduler_test_fault(4);
#endif
    phase = 3; x86_scheduler_unbind();
    return kernel_log(0, "KERNEL_SCHEDULER_OK");
}

// ====== next_session_private ======

static int next_session_private(uint32_t previous)
{
    if (phase != 3 || irq_profile != previous || previous >= 5 || !saved_pool || current != KERNEL_SCHEDULER_NONE) return 0;
    irq_profile = previous + 1; phase = 0; stats = (X86_DISPATCH_STATS){0};
    return kernel_scheduler_run(saved_pool, saved_root);
}

#ifdef KERNEL_PS2_INPUT
// ====== x86_scheduler_run_input ======

int x86_scheduler_run_input(void) { return next_session_private(4); }
#endif

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

// ====== x86_scheduler_idle_retained_valid ======

int x86_scheduler_idle_retained_valid(void)
{
    return (irq_profile == 2 || irq_profile == 4) && stats.waiting_count == 2 && x86_scheduler_retained_valid();
}

// ====== x86_scheduler_retained_valid ======

int x86_scheduler_retained_valid(void)
{
    if (phase != 1 || current != KERNEL_SCHEDULER_NONE || stats.completed || !active_profile ||
        !kernel_scheduler_context_valid() || !kernel_physical_validate(saved_pool) ||
        active_profile->task_count < 2 || active_profile->task_count > 8 ||
        budget < 16 * active_profile->task_count || !kernel_physical_budget_matches(saved_pool, budget - 16 * active_profile->task_count)) return 0;
    for (uint32_t index = 0; index < active_profile->task_count; ++index)
        if (!x86_task_stack_validate(&stacks[index])) return 0;
    return 1;
}

// ====== x86_scheduler_session_stats ======

const X86_DISPATCH_STATS *x86_scheduler_session_stats(void) { return &stats; }

// ====== x86_scheduler_run_idle_repeat ======

int x86_scheduler_run_idle_repeat(void)
{
    return next_session_private(3);
}

// ====== x86_scheduler_irq_timeout_valid ======

int x86_scheduler_irq_timeout_valid(void)
{
    if (!irq_profile || phase != 2 || current != 1 || stats.waiting_count != 1 || stats.completed ||
        !x86_scheduler_task_valid() || !kernel_physical_validate(saved_pool) ||
        !kernel_physical_budget_matches(saved_pool, budget - 32)) return 0;
    // Validate retained leases from a task without invoking dispatcher-only APIs.
    for (uint32_t index = 0; index < 2; ++index) {
        if (stacks[index].self != (uint64_t)(uintptr_t)&stacks[index] || stacks[index].index != index ||
            stacks[index].pool != saved_pool || stacks[index].root != saved_root ||
            !stacks[index].generation || stacks[index].stage != X86_TASK_STACK_READY ||
            !x86_task_stack_mapping_valid(&stacks[index],1) ||
            kernel_physical_is_pinned(saved_pool,&stacks[index].lease) != 1) return 0;
    }
    return 1;
}

// ====== x86_scheduler_irq_stack_top ======

uint64_t x86_scheduler_irq_stack_top(uint64_t frame)
{
    uint64_t base;
    if (!irq_profile || !x86_scheduler_task_valid() || current >= 8 || (frame & 7)) return 0;
    base = x86_task_stack_base(current);
    return frame >= base && frame <= base + 65536 - 48 ? base + 65536 : 0;
}
