#include "task.h"
#include "kernel/shell/shell.h"
#include "kernel/diagnostics/log.h"
#include "kernel/sync/lock.h"
#include "arch/x86/kernel/scheduler/remote/remote.h"
#include "arch/x86/kernel/drivers/video/framebuffer/console/wake/notify.h"
#include "arch/x86/kernel/cpu/percpu/service/native/native.h"

// ====== job_private ======

static void job_private(uint32_t index)
{
    uint64_t saved_if = kernel_preempt_section_enter();
    X86_REMOTE *job = x86_remote_job();
    if (!job ||
        index != job->Index ||
        xis_native_index() != job->Cpu ||
        !x86_scheduler_task_valid() ||
        x86_scheduler_driver_task_valid() ||
        !job->Argument ||
        job->Argument > KERNEL_SHELL_AP_MAX
    )
        xis_native_abort(0x4b34415053484a42ULL);
    kernel_preempt_section_leave(saved_if);
    uint64_t sum = 0, check = 0xcbf29ce484222325ULL;
    for (uint64_t value = 1; value <= job->Argument; ++value) {
        sum += value;
        check = (check ^ value) * 0x100000001b3ULL;
    }
    saved_if = kernel_preempt_section_enter();
    job->Result[0] = xis_native_index();
    job->Result[1] = sum;
    job->Result[2] = check;
    kernel_preempt_section_leave(saved_if);
}

// ====== event_private ======

static KERNEL_EVENT_REPLY event_private(uint32_t reason, uint64_t token)
{
    KERNEL_EVENT_REPLY reply = x86_scheduler_event_request(reason,XCN_EVENT,token);
    if (reply.reply.status != KERNEL_SCHEDULER_OK) kernel_scheduler_fail_stop();
    return reply;
}

// ====== kernel_shell_remote_on ======

int kernel_shell_remote_on(uint32_t cpu, uint64_t argument, uint64_t result[4])
{
    uint64_t waits = 0;
    if (!result ||
        x86_scheduler_index() != 2 ||
        !x86_scheduler_driver_task_valid() ||
        !argument ||
        argument > KERNEL_SHELL_AP_MAX
    )
        return 0;
    uint64_t saved_if = kernel_preempt_section_enter();
    int requested = x86_remote_request(cpu,job_private,argument);
    kernel_preempt_section_leave(saved_if);
    if (!requested) return 0;
    for (uint32_t round = 0; round < 4096; ++round) {
        KERNEL_EVENT_REPLY observed = event_private(X86_TASK_OBSERVE,0);
        int taken = x86_remote_take(result);
        if (taken) {
            result[3] = waits;
            return taken == 1;
        }
        KERNEL_EVENT_REPLY waited = event_private(X86_TASK_WAIT,observed.epoch);
        if (waited.outcome == KERNEL_EVENT_WAITING) ++waits;
    }
    kernel_scheduler_fail_stop();
}

// ====== kernel_shell_remote ======

int kernel_shell_remote(uint64_t argument, uint64_t result[4]) {
    return kernel_shell_remote_on(1,argument,result);
}
