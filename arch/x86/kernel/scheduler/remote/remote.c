#include "remote.h"
#include "arch/x86/kernel/cpu/percpu/service/native/native.h"
#include "arch/x86/kernel/timer/hpet/watchdog/watchdog.h"
#include "kernel/sync/internal.h"
#include "kernel/diagnostics/log.h"
#include "../irq/preempt.h"
#include "../integration/local.h"

static X86_REMOTE remote;
static const KERNEL_PHYSICAL_ALLOCATOR *owner_pool;

// ====== now_private ======

static uint32_t now_private(void)
{
    uint32_t now = 0;
    if (!xis_native_now(NULL,&now)) kernel_panic("REMOTE_CLOCK_FAILED");
    return now;
}

// ====== expired_private ======

static int expired_private(uint64_t start)
{
    return ((now_private() - (uint32_t)start) & xp_panic_native.Source.Mask) >= KERNEL_PM_TIMER_HZ;
}

// ====== ping_private ======

static int ping_private(uint32_t cpu)
{
    uint64_t token = xis_publish(cpu,0);
    XIS_WAIT wait = {xis_native_now,NULL,xp_panic_native.Source.Mask,KERNEL_PM_TIMER_HZ,1000000,0};
    return token && token != XIS_INVALID && xis_native_send(cpu) &&
        xis_wait(cpu,token,&wait) == XIS_WAIT_ACK;
}

// ====== x86_remote_open ======

int x86_remote_open(KERNEL_PHYSICAL_ALLOCATOR *pool, uint64_t root, uint32_t event,
    uint64_t source, uint64_t binding)
{
    if (!x86_percpu_bsp() || ks_irq_state() || remote.Self || !pool || !root || event >= 8 || !source ||
        !binding || xis_native.Count < 2) return 0;
    owner_pool = pool;
    remote = (X86_REMOTE){0};
    remote.Root = root; remote.Event = event; remote.EventSource = source; remote.EventBinding = binding;
    remote.Index = UINT32_MAX;
    remote.Self = (uint64_t)(uintptr_t)&remote;
    return 1;
}

// ====== x86_remote_request ======

int x86_remote_request(uint32_t cpu, X86_TASK_ENTRY entry, uint64_t argument)
{
    if (!remote.Self ||
        (cpu != X86_REMOTE_ANY && cpu >= xis_native.Count) ||
        !entry ||
        !x86_scheduler_code_valid((uint64_t)(uintptr_t)entry) ||
        ks_atomic_load_acquire(&remote.State) != X86_REMOTE_IDLE
    )
        return 0;
    remote.Cpu = cpu; remote.Entry = entry; remote.Argument = argument;
    remote.Allowed = cpu == X86_REMOTE_ANY ?
        (xis_native.Count == 64 ? UINT64_MAX : (1ULL << xis_native.Count) - 1) : 1ULL << cpu;
    remote.Visits = 0;
    remote.Migrations = 0;
    for (uint32_t index = 0; index < 4; ++index) remote.Result[index] = 0;
    ks_atomic_store_release(&remote.State,X86_REMOTE_REQUESTED);
    return 1;
}

// ====== failed_private ======

static void failed_private(uint32_t failure)
{
    remote.Failure = failure;
    ks_atomic_store_release(&remote.State,X86_REMOTE_FAILED);
    if (!kernel_scheduler_irq_post_bound(remote.Event,remote.EventSource,remote.EventBinding))
        kernel_panic("REMOTE_EVENT_FAILED");
}

// ====== rollback_private ======

static void rollback_private(uint32_t index, uint64_t generation)
{
    if (x86_task_stacks[index].self && !x86_task_stack_release(&x86_task_stacks[index],index,generation))
        kernel_panic("REMOTE_ROLLBACK_FAILED");
    if (kernel_scheduler_apply_on(0,KERNEL_SCHEDULER_ABORT,index,generation).status)
        kernel_panic("REMOTE_ROLLBACK_FAILED");
    failed_private(1);
}

// ====== launch_private ======

static void launch_private(void)
{
    KERNEL_SCHEDULER_REPLY reply = kernel_scheduler_apply_on(0,KERNEL_SCHEDULER_RESERVE,KERNEL_SCHEDULER_NONE,0);
    if (reply.status || reply.index >= x86_scheduler_capacity() || reply.state != KERNEL_TASK_RESERVED || !reply.generation) {
        failed_private(2);
        return;
    }
    uint32_t index = reply.index;
    uint64_t generation = reply.generation;
    if (!x86_scheduler_task_assign_mask(index,remote.Allowed,remote.Entry) ||
        kernel_scheduler_affinity_on(0,index,generation,remote.Allowed) ||
        !x86_task_stack_acquire(&x86_task_stacks[index],(KERNEL_PHYSICAL_ALLOCATOR *)owner_pool,remote.Root,
            index,generation) ||
        !x86_scheduler_context_prepare(&x86_task_contexts[index],x86_task_stack_base(index),65536) ||
        (x86_watchdog_budget_active() && !x86_watchdog_budget_lend(&x86_task_stacks[index].lease))) {
        rollback_private(index,generation);
        return;
    }
    remote.Index = index; remote.Generation = generation;
    ks_atomic_store_release(&remote.Quiesced,0);
    if (!xis_native_shootdown()) kernel_panic("REMOTE_SHOOTDOWN_FAILED");
    if (kernel_scheduler_apply_on(0,KERNEL_SCHEDULER_PUBLISH,index,generation).state != KERNEL_TASK_READY)
        kernel_panic("REMOTE_PUBLISH_FAILED");
    for (uint32_t cpu = 1; cpu < xis_native.Count; ++cpu) {
        if ((remote.Allowed & (1ULL << cpu)) && !ping_private(cpu))
            kernel_panic("REMOTE_PING_FAILED");
    }
    remote.Progress = ks_atomic_load_acquire(&x86_scheduler_task_binding(index)->Dispatches);
    remote.ProgressAt = now_private();
    ks_atomic_store_release(&remote.State,X86_REMOTE_STARTED);
}

// ====== finish_private ======

static void finish_private(void)
{
    uint32_t index = remote.Index;
    const X86_TASK_BINDING *binding = x86_scheduler_task_binding(index);
    if (!binding ||
        ks_atomic_load_acquire(&binding->Owner) ||
        binding->LastCpu >= xis_native.Count
    )
        kernel_panic("REMOTE_OWNER_RETAINED");
    remote.Result[0] = binding->LastCpu;
    remote.Visits = binding->Visits;
    remote.Migrations = binding->Migrations;
    KERNEL_SCHEDULER_REPLY query = kernel_scheduler_apply_on(0,KERNEL_SCHEDULER_QUERY,index,remote.Generation);
    if (query.status || query.state != KERNEL_TASK_FINISHED ||
        (x86_watchdog_budget_active() && !x86_watchdog_budget_return(&x86_task_stacks[index].lease)) ||
        !x86_task_stack_release(&x86_task_stacks[index],index,remote.Generation) ||
        kernel_scheduler_apply_on(0,KERNEL_SCHEDULER_COLLECT,index,remote.Generation).status)
        kernel_panic("REMOTE_COLLECT_FAILED");
    remote.Index = UINT32_MAX; remote.Generation = 0;
    remote.Done = now_private();
    ks_atomic_store_release(&remote.State,X86_REMOTE_DONE);
    if (!kernel_scheduler_irq_post_bound(remote.Event,remote.EventSource,remote.EventBinding))
        kernel_panic("REMOTE_EVENT_FAILED");
}

// ====== x86_remote_service ======

void x86_remote_service(void)
{
    if (!remote.Self || !x86_percpu_bsp() || ks_irq_state()) return;
    uint32_t state = (uint32_t)ks_atomic_load_acquire(&remote.State);
    if (state == X86_REMOTE_REQUESTED) launch_private();
    else if (state == X86_REMOTE_STARTED) {
        if (ks_atomic_load_acquire(&remote.Quiesced) == remote.Generation) finish_private();
        else {
            const X86_TASK_BINDING *binding = x86_scheduler_task_binding(remote.Index);
            uint64_t progress = ks_atomic_load_acquire(&binding->Dispatches);
            if (progress != remote.Progress) {
                remote.Progress = progress;
                remote.ProgressAt = now_private();
            }
            else if (expired_private(remote.ProgressAt)
            ) {
                progress = ks_atomic_load_acquire(&binding->Dispatches);
                if (ks_atomic_load_acquire(&remote.Quiesced) == remote.Generation
                )
                    finish_private();
                else if (progress != remote.Progress) {
                    remote.Progress = progress;
                    remote.ProgressAt = now_private();
                }
                else
                    kernel_panic("REMOTE_TASK_FAILED");
            }
        }
    }
}

// ====== x86_remote_state ======

uint32_t x86_remote_state(void)
{
    return remote.Self ? (uint32_t)ks_atomic_load_acquire(&remote.State) : X86_REMOTE_FAILED;
}

// ====== x86_remote_job ======

X86_REMOTE *x86_remote_job(void) { return remote.Self ? &remote : NULL; }

// ====== x86_remote_take ======

int x86_remote_take(uint64_t result[4])
{
    uint32_t state = x86_remote_state();
    if (!result || (state != X86_REMOTE_DONE && state != X86_REMOTE_FAILED)) return 0;
    for (uint32_t index = 0; index < 4; ++index) result[index] = remote.Result[index];
    ++remote.Jobs;
    ks_atomic_store_release(&remote.State,X86_REMOTE_IDLE);
    return state == X86_REMOTE_DONE ? 1 : -1;
}

// ====== x86_remote_statistics ======

int x86_remote_statistics(uint64_t result[2]) {
    if (!result ||
        !remote.Self ||
        x86_remote_state() != X86_REMOTE_IDLE ||
        !remote.Jobs
    )
        return 0;

    result[0] = remote.Visits;
    result[1] = remote.Migrations;
    return 1;
}

// ====== x86_remote_close ======

int x86_remote_close(void)
{
    if (!remote.Self || !x86_percpu_bsp() || x86_remote_state() != X86_REMOTE_IDLE ||
        !x86_ap_scope() || xad_value(1,remote.Root) != (x86_ap_scope()->Token << 3 | XAD_RUNNING)) return 0;
    remote = (X86_REMOTE){0};
    owner_pool = NULL;
    return 1;
}

// ====== x86_remote_dispatch_collect ======

int x86_remote_dispatch_collect(const KERNEL_SCHEDULER_REPLY *task) {
    X86_SCHEDULER_LOCAL *local = x86_scheduler_local_current();
    return local && task && task->index == remote.Index && task->generation == remote.Generation &&
        task->state == KERNEL_TASK_FINISHED && local->current == task->index &&
        local->generation == task->generation &&
        (remote.Allowed & (1ULL << xis_native_index())) != 0;
}

// ====== x86_remote_dispatch_release ======

int x86_remote_dispatch_release(void) {
    return x86_scheduler_dispatch_release();
}

// ====== x86_remote_depart ======

void x86_remote_depart(uint32_t index, uint64_t generation, uint32_t reason) {
    if (reason == X86_TASK_EXIT &&
        remote.Self && index == remote.Index && generation == remote.Generation
    )
        ks_atomic_store_release(&remote.Quiesced,generation);
}

// ====== x86_remote_jobs ======

uint32_t x86_remote_jobs(void) { return remote.Self ? remote.Jobs : 0; }
