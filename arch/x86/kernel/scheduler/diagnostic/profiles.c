#include "profiles.h"
#include "../integration/control.h"
#include "../idle/idle.h"
#include "../persistent/persistent.h"
#include "kernel/diagnostics/log.h"
#ifdef KERNEL_PS2_INPUT
#include "arch/x86/kernel/drivers/input/ps2/runtime/session.h"
#endif

// ====== baseline_entry_private ======

static void baseline_entry_private(uint32_t index)
{
    (void)index;
    x86_scheduler_workload();
}

// ====== x86_scheduler_profile_blocked ======

int x86_scheduler_profile_blocked(void)
{
    const X86_DISPATCH_STATS *stats = x86_scheduler_session_stats();
    if (!stats || stats->steps != 202 || stats->waiting_count != 2 || stats->changed_count ||
        stats->signal_count || stats->completed || !x86_scheduler_retained_valid())
        kernel_panic("SCHEDULER_BLOCKED_STATE_INVALID");
    if (!kernel_log(1,"KERNEL_SCHEDULER_BLOCKED_OK")) kernel_scheduler_fail_stop();
    kernel_scheduler_fail_stop();
}

static const X86_SCHEDULER_PROFILE profiles[] = {
#if defined(KERNEL_TEST_SCHEDULER_FAULT) && KERNEL_TEST_SCHEDULER_FAULT == 6
    {2,210,1,0,baseline_entry_private,x86_scheduler_profile_blocked},
#else
    {2,210,0,0,baseline_entry_private,NULL},
#endif
    {2,3,0,1,x86_scheduler_irq_task,NULL},
    {2,6,1,1,x86_scheduler_idle_task,x86_scheduler_idle_wait},
    {2,1024,0,1,x86_scheduler_persistent_task,NULL},
    {2,6,1,1,x86_scheduler_idle_task,x86_scheduler_idle_wait}
#ifdef KERNEL_PS2_INPUT
#ifdef KERNEL_PS2_MOUSE
    ,{4,65536,32768,1,x86_ps2_task,x86_ps2_idle}
#else
    ,{3,65536,32768,1,x86_ps2_task,x86_ps2_idle}
#endif
#endif
};

// ====== x86_scheduler_profile ======

const X86_SCHEDULER_PROFILE *x86_scheduler_profile(uint32_t id)
{
    return id < sizeof(profiles) / sizeof(profiles[0]) ? &profiles[id] : NULL;
}

// ====== x86_scheduler_profile_complete ======

int x86_scheduler_profile_complete(uint32_t id, const X86_DISPATCH_STATS *stats)
{
    const X86_SCHEDULER_PROFILE *profile = x86_scheduler_profile(id);
    if (!profile || !stats || stats->failure || stats->completed != profile->task_count ||
        stats->steps > profile->step_limit || stats->idle_calls > profile->idle_limit) return 0;
#ifdef KERNEL_PS2_INPUT
    if (id == 5) return x86_ps2_complete();
#endif
    if (id == 0) return stats->steps == 210 && stats->waiting_count == 1 && stats->changed_count == 1 &&
        stats->signal_count == 2 && !stats->stop_waits && x86_scheduler_workload_complete();
    if (id == 1) return stats->steps == 3 && stats->waiting_count == 1 && !stats->changed_count &&
        !stats->signal_count && !stats->stop_waits && x86_scheduler_irq_verified();
    if (id == 3) return stats->steps == 136 && stats->waiting_count == 33 && !stats->changed_count &&
        stats->signal_count == 32 && stats->stop_waits == 1 && x86_scheduler_persistent_complete();
    return stats->steps == 6 && stats->waiting_count == 2 && !stats->changed_count && !stats->signal_count &&
        !stats->stop_waits && stats->idle_calls == 1 && x86_scheduler_idle_verified();
}
