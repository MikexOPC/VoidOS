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

// ====== x86_remote_request_batch ======

int x86_remote_request_batch(uint32_t cpu, uint32_t count, X86_TASK_ENTRY entry, uint64_t argument)
{
    if (!remote.Self ||
        !count ||
        count > X86_REMOTE_BATCH_MAX ||
        (cpu != X86_REMOTE_ANY && cpu >= xis_native.Count) ||
        !entry ||
        !x86_scheduler_code_valid((uint64_t)(uintptr_t)entry) ||
        ks_atomic_load_acquire(&remote.State) != X86_REMOTE_IDLE
    )
        return 0;
    remote.Cpu = cpu; remote.Entry = entry; remote.Argument = argument;
    remote.BatchCount = count;
    remote.MaxIndex = 0;
    remote.Allowed = cpu == X86_REMOTE_ANY ?
        (xis_native.Count == 64 ? UINT64_MAX : (1ULL << xis_native.Count) - 1) : 1ULL << cpu;
    remote.Visits = 0;
    remote.Migrations = 0;
    for (uint32_t index = 0; index < 4; ++index) remote.Result[index] = 0;
    for (uint32_t slot = 0; slot < X86_REMOTE_BATCH_MAX; ++slot) {
        remote.Indices[slot] = UINT32_MAX;
        remote.Generations[slot] = 0;
        ks_atomic_store_release(&remote.QuiescedBatch[slot], 0);
        remote.BatchResults[slot][0] = 0;
        remote.BatchResults[slot][1] = 0;
        remote.BatchResults[slot][2] = 0;
    }
    ks_atomic_store_release(&remote.State,X86_REMOTE_REQUESTED);
    return 1;
}

// ====== x86_remote_request ======

int x86_remote_request(uint32_t cpu, X86_TASK_ENTRY entry, uint64_t argument)
{
    return x86_remote_request_batch(cpu, 1, entry, argument);
}

// ====== failed_private ======

static void failed_private(uint32_t failure)
{
    remote.Failure = failure;
    ks_atomic_store_release(&remote.State,X86_REMOTE_FAILED);
    if (!kernel_scheduler_irq_post_bound(remote.Event,remote.EventSource,remote.EventBinding))
        kernel_panic("REMOTE_EVENT_FAILED");
}

// ====== rollback_batch_private ======

static void rollback_batch_private(uint32_t prepared)
{
    for (uint32_t slot = prepared; slot > 0; --slot) {
        uint32_t index = remote.Indices[slot - 1];
        uint64_t generation = remote.Generations[slot - 1];
        if (x86_task_stacks[index].self) {
            if (x86_watchdog_budget_active()) (void)x86_watchdog_budget_return(&x86_task_stacks[index].lease);
            if (!x86_task_stack_release(&x86_task_stacks[index], index, generation))
                kernel_panic("REMOTE_ROLLBACK_FAILED");
        }
        if (kernel_scheduler_apply_on(0, KERNEL_SCHEDULER_ABORT, index, generation).status)
            kernel_panic("REMOTE_ROLLBACK_FAILED");
        remote.Indices[slot - 1] = UINT32_MAX;
        remote.Generations[slot - 1] = 0;
    }
    remote.Index = UINT32_MAX; remote.Generation = 0;
    failed_private(1);
}

// ====== batch_progress_private ======

static uint64_t batch_progress_private(void)
{
    uint64_t total = 0;
    for (uint32_t slot = 0; slot < remote.BatchCount; ++slot) {
        const X86_TASK_BINDING *binding = x86_scheduler_task_binding(remote.Indices[slot]);
        if (binding) total += ks_atomic_load_acquire(&binding->Dispatches);
    }
    return total;
}

// ====== batch_all_quiesced_private ======

static int batch_all_quiesced_private(void)
{
    for (uint32_t slot = 0; slot < remote.BatchCount; ++slot) {
        if (ks_atomic_load_acquire(&remote.QuiescedBatch[slot]) != remote.Generations[slot])
            return 0;
    }
    return remote.BatchCount > 0;
}

// ====== launch_private ======

static void launch_private(void)
{
    uint32_t count = remote.BatchCount;
    if (!count || count > X86_REMOTE_BATCH_MAX) {
        failed_private(2);
        return;
    }
    uint32_t max_index = 0;
    for (uint32_t slot = 0; slot < count; ++slot) {
        KERNEL_SCHEDULER_REPLY reply = kernel_scheduler_apply_on(0,KERNEL_SCHEDULER_RESERVE,KERNEL_SCHEDULER_NONE,0);
        if (reply.status || reply.index >= x86_scheduler_capacity() || reply.state != KERNEL_TASK_RESERVED || !reply.generation) {
            rollback_batch_private(slot);
            return;
        }
        uint32_t index = reply.index;
        uint64_t generation = reply.generation;
        remote.Indices[slot] = index;
        remote.Generations[slot] = generation;
        if (index > max_index) max_index = index;
        if (!x86_scheduler_task_assign_mask(index,remote.Allowed,remote.Entry) ||
            kernel_scheduler_affinity_on(0,index,generation,remote.Allowed) ||
            !x86_task_stack_acquire(&x86_task_stacks[index],(KERNEL_PHYSICAL_ALLOCATOR *)owner_pool,remote.Root,
                index,generation) ||
            !x86_scheduler_context_prepare(&x86_task_contexts[index],x86_task_stack_base(index),65536) ||
            (x86_watchdog_budget_active() && !x86_watchdog_budget_lend(&x86_task_stacks[index].lease))) {
            rollback_batch_private(slot + 1);
            return;
        }
    }
    remote.Index = remote.Indices[0];
    remote.Generation = remote.Generations[0];
    remote.MaxIndex = max_index;
    ks_atomic_store_release(&remote.Quiesced,0);
    if (!xis_native_shootdown()) kernel_panic("REMOTE_SHOOTDOWN_FAILED");
    for (uint32_t slot = 0; slot < count; ++slot) {
        if (kernel_scheduler_apply_on(0,KERNEL_SCHEDULER_PUBLISH,remote.Indices[slot],remote.Generations[slot]).state != KERNEL_TASK_READY)
            kernel_panic("REMOTE_PUBLISH_FAILED");
    }
    for (uint32_t cpu = 1; cpu < xis_native.Count; ++cpu) {
        if ((remote.Allowed & (1ULL << cpu)) && !ping_private(cpu))
            kernel_panic("REMOTE_PING_FAILED");
    }
    remote.Progress = batch_progress_private();
    remote.ProgressAt = now_private();
    ks_atomic_store_release(&remote.State,X86_REMOTE_STARTED);
}

// ====== finish_private ======

static void finish_private(void)
{
    uint64_t expected_sum = remote.BatchResults[0][1];
    uint64_t expected_check = remote.BatchResults[0][2];
    uint64_t visits = 0, migrations = 0;
    uint32_t count = remote.BatchCount;
    for (uint32_t slot = 0; slot < count; ++slot) {
        uint32_t index = remote.Indices[slot];
        uint64_t generation = remote.Generations[slot];
        const X86_TASK_BINDING *binding = x86_scheduler_task_binding(index);
        if (!binding ||
            ks_atomic_load_acquire(&binding->Owner) ||
            binding->LastCpu >= xis_native.Count
        )
            kernel_panic("REMOTE_OWNER_RETAINED");
        if (remote.BatchResults[slot][1] != expected_sum ||
            remote.BatchResults[slot][2] != expected_check)
            kernel_panic("REMOTE_BATCH_MISMATCH");
        visits |= binding->Visits;
        migrations += binding->Migrations;
        if (slot == 0) {
            remote.Result[0] = binding->LastCpu;
            remote.Result[1] = expected_sum;
            remote.Result[2] = expected_check;
        }
        KERNEL_SCHEDULER_REPLY query = kernel_scheduler_apply_on(0,KERNEL_SCHEDULER_QUERY,index,generation);
        if (query.status || query.state != KERNEL_TASK_FINISHED ||
            (x86_watchdog_budget_active() && !x86_watchdog_budget_return(&x86_task_stacks[index].lease)) ||
            !x86_task_stack_release(&x86_task_stacks[index],index,generation) ||
            kernel_scheduler_apply_on(0,KERNEL_SCHEDULER_COLLECT,index,generation).status)
            kernel_panic("REMOTE_COLLECT_FAILED");
        remote.Indices[slot] = UINT32_MAX;
        remote.Generations[slot] = 0;
    }
    remote.Visits = visits;
    remote.Migrations = migrations;
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
        if (batch_all_quiesced_private()) finish_private();
        else {
            uint64_t progress = batch_progress_private();
            if (progress != remote.Progress) {
                remote.Progress = progress;
                remote.ProgressAt = now_private();
            }
            else if (expired_private(remote.ProgressAt)
            ) {
                progress = batch_progress_private();
                if (batch_all_quiesced_private()
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

// ====== x86_remote_batch_info ======

int x86_remote_batch_info(uint64_t result[2]) {
    if (!result ||
        !remote.Self ||
        x86_remote_state() != X86_REMOTE_IDLE ||
        !remote.Jobs
    )
        return 0;

    result[0] = remote.BatchCount;
    result[1] = remote.MaxIndex;
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

// ====== batch_slot_private ======

static int batch_slot_private(uint32_t index, uint64_t generation, uint32_t *out)
{
    if (!remote.Self || !generation) return 0;
    for (uint32_t slot = 0; slot < remote.BatchCount; ++slot) {
        if (remote.Indices[slot] == index && remote.Generations[slot] == generation) {
            if (out) *out = slot;
            return 1;
        }
    }
    return 0;
}

// ====== x86_remote_dispatch_collect ======

int x86_remote_dispatch_collect(const KERNEL_SCHEDULER_REPLY *task) {
    X86_SCHEDULER_LOCAL *local = x86_scheduler_local_current();
    return local && task && batch_slot_private(task->index, task->generation, NULL) &&
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
    uint32_t slot = 0;
    if (reason == X86_TASK_EXIT && batch_slot_private(index, generation, &slot)) {
        ks_atomic_store_release(&remote.QuiescedBatch[slot], generation);
        if (slot == 0) ks_atomic_store_release(&remote.Quiesced, generation);
    }
}

// ====== x86_remote_jobs ======

uint32_t x86_remote_jobs(void) { return remote.Self ? remote.Jobs : 0; }
