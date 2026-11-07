#include "remote.h"
#include "arch/x86/kernel/cpu/percpu/service/native/native.h"
#include "arch/x86/kernel/timer/hpet/watchdog/watchdog.h"
#include "kernel/sync/internal.h"
#include "kernel/diagnostics/log.h"

static X86_REMOTE remote;
static X86_AP_SNAPSHOT arena;
static const KERNEL_PHYSICAL_ALLOCATOR *owner_pool;
static uint64_t tokens, configured;

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

static int ping_private(uint32_t cpu, uint32_t idle)
{
    uint64_t token = xis_publish(cpu,0);
    XIS_WAIT wait = {xis_native_now,NULL,xp_panic_native.Source.Mask,KERNEL_PM_TIMER_HZ,1000000,0};
    return token && token != XIS_INVALID && xis_native_send(cpu) &&
        xis_wait(cpu,token,&wait) == XIS_WAIT_ACK && (!idle || xis_native_wait(cpu,1,token));
}

// ====== scope_private ======

static int scope_private(uint64_t token)
{
    X86_AP_SCOPE scope = {remote.Root,token,owner_pool,arena,xis_native.Count - 1,0};
    x86_ap_scope_unbind();
    return x86_ap_scope_bind(&scope);
}

// ====== x86_remote_open ======

int x86_remote_open(KERNEL_PHYSICAL_ALLOCATOR *pool, uint64_t root, uint32_t event,
    uint64_t source, uint64_t binding)
{
    if (!x86_percpu_bsp() || ks_irq_state() || remote.Self || !pool || !root || event >= 8 || !source ||
        !binding || xis_native.Count < 2) return 0;
    if (!configured) {
        if (!xad_configure(xis_native.Count,root,0)) return 0;
        configured = 1;
    }
    if (!x86_ap_inputs(pool,root,(uint64_t)(uintptr_t)&arena,sizeof(arena),&arena)) return 0;
    owner_pool = pool;
    remote = (X86_REMOTE){0};
    remote.Root = root; remote.Event = event; remote.EventSource = source; remote.EventBinding = binding;
    remote.Index = UINT32_MAX;
    if (!scope_private(tokens + 1)) return 0;
    remote.Self = (uint64_t)(uintptr_t)&remote;
    return 1;
}

// ====== x86_remote_request ======

int x86_remote_request(uint32_t cpu, X86_TASK_ENTRY entry, uint64_t argument)
{
    if (!remote.Self || cpu != 1 || cpu >= xis_native.Count || !entry ||
        !x86_scheduler_code_valid((uint64_t)(uintptr_t)entry) ||
        ks_atomic_load_acquire(&remote.State) != X86_REMOTE_IDLE) return 0;
    remote.Cpu = cpu; remote.Entry = entry; remote.Argument = argument;
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
    if (reply.status || reply.index >= 8 || reply.state != KERNEL_TASK_RESERVED || !reply.generation) {
        failed_private(2);
        return;
    }
    uint32_t index = reply.index;
    uint64_t generation = reply.generation;
    if (!x86_scheduler_task_assign(index,remote.Cpu,remote.Entry) ||
        kernel_scheduler_affinity_on(0,index,generation,1ULL << remote.Cpu) ||
        !x86_task_stack_acquire(&x86_task_stacks[index],(KERNEL_PHYSICAL_ALLOCATOR *)owner_pool,remote.Root,
            index,generation) ||
        !x86_scheduler_context_prepare(&x86_task_contexts[index],x86_task_stack_base(index),65536) ||
        (x86_watchdog_budget_active() && !x86_watchdog_budget_lend(&x86_task_stacks[index].lease))) {
        rollback_private(index,generation);
        return;
    }
    remote.Index = index; remote.Generation = generation; remote.Token = ++tokens;
    if (kernel_scheduler_apply_on(0,KERNEL_SCHEDULER_PUBLISH,index,generation).state != KERNEL_TASK_READY)
        kernel_panic("REMOTE_PUBLISH_FAILED");
    if (!xis_native_shootdown()) kernel_panic("REMOTE_SHOOTDOWN_FAILED");
    if (!scope_private(remote.Token)) kernel_panic("REMOTE_SCOPE_FAILED");
    if (!xad_offer(remote.Cpu,0,remote.Token,remote.Root)) kernel_panic("REMOTE_OFFER_FAILED");
    if (!ping_private(remote.Cpu,0)) kernel_panic("REMOTE_PING_FAILED");
    remote.Started = now_private();
    ks_atomic_store_release(&remote.State,X86_REMOTE_OFFERED);
}

// ====== finish_private ======

static void finish_private(void)
{
    uint32_t index = remote.Index;
    KERNEL_SCHEDULER_REPLY query = kernel_scheduler_apply_on(0,KERNEL_SCHEDULER_QUERY,index,remote.Generation);
    if (query.status || query.state != KERNEL_TASK_FINISHED ||
        (x86_watchdog_budget_active() && !x86_watchdog_budget_return(&x86_task_stacks[index].lease)) ||
        !x86_task_stack_release(&x86_task_stacks[index],index,remote.Generation) ||
        kernel_scheduler_apply_on(0,KERNEL_SCHEDULER_COLLECT,index,remote.Generation).status ||
        !xad_rearm(remote.Cpu,0,remote.Token,remote.Root) || !scope_private(tokens + 1))
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
    else if (state == X86_REMOTE_OFFERED) {
        uint64_t value = xad_value(remote.Cpu,remote.Root);
        if (value == (remote.Token << 3 | XAD_ADMITTED)) {
            if (!xad_transition(remote.Cpu,0,remote.Token,remote.Root,0) || !ping_private(remote.Cpu,0))
                kernel_panic("REMOTE_START_FAILED");
            ks_atomic_store_release(&remote.State,X86_REMOTE_STARTED);
        } else if ((value & 7) == XAD_FAILED || expired_private(remote.Started)) kernel_panic("REMOTE_ADMISSION_FAILED");
    } else if (state == X86_REMOTE_STARTED) {
        if (xad_collect_allowed(remote.Cpu,0,remote.Token,remote.Root) == 1) finish_private();
        else if ((xad_value(remote.Cpu,remote.Root) & 7) == XAD_FAILED || expired_private(remote.Started))
            kernel_panic("REMOTE_TASK_FAILED");
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

// ====== x86_remote_close ======

int x86_remote_close(void)
{
    if (!remote.Self || !x86_percpu_bsp() || x86_remote_state() != X86_REMOTE_IDLE ||
        xad_value(1,remote.Root) != 0) return 0;
    x86_ap_scope_unbind();
    remote = (X86_REMOTE){0};
    owner_pool = NULL;
    return 1;
}

// ====== retain_private ======

static int retain_private(const KERNEL_SCHEDULER_REPLY *task)
{
    return task->index == remote.Index && task->generation == remote.Generation &&
        task->state == KERNEL_TASK_FINISHED;
}

// ====== x86_remote_ap_run ======

void x86_remote_ap_run(void)
{
    X86_SCHEDULER_LOCAL *local = x86_scheduler_local_current();
    uint32_t cpu = xis_native_index();
    if (!local || local->phase || local->current != KERNEL_SCHEDULER_NONE || !x86_ap_actor() || cpu != remote.Cpu)
        xis_native_abort(0x4b3452454d4f5445ULL);
    local->phase = 1;
    X86_DISPATCH_CONFIG config = {1,64,0,0,x86_scheduler_dispatch_run,retain_private,
        x86_scheduler_dispatch_reply,x86_scheduler_dispatch_release,NULL};
    int ran = x86_scheduler_dispatch_retained_on(cpu,&config,&local->stats);
    local->stats = (X86_DISPATCH_STATS){0};
    local->phase = 0;
    if (!ran || local->current != KERNEL_SCHEDULER_NONE) xis_native_abort(0x4b3452454d4f5445ULL);
}

// ====== x86_remote_jobs ======

uint32_t x86_remote_jobs(void) { return remote.Self ? remote.Jobs : 0; }
