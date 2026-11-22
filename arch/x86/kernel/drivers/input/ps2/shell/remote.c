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
    uint32_t slot = UINT32_MAX;
    if (job) {
        for (uint32_t candidate = 0; candidate < job->BatchCount; ++candidate) {
            if (job->Indices[candidate] == index) { slot = candidate; break; }
        }
    }
    if (!job ||
        slot >= X86_REMOTE_BATCH_MAX ||
        !(job->Allowed & (1ULL << xis_native_index())) ||
        !x86_scheduler_task_valid() ||
        x86_scheduler_driver_task_valid() ||
        !job->Argument ||
        job->Argument > KERNEL_SHELL_AP_MAX
    )
        xis_native_abort(0x4b34415053484a42ULL);
    uint64_t limit = job->Argument;
    kernel_preempt_section_leave(saved_if);
    uint64_t sum = 0, check = 0xcbf29ce484222325ULL;
    for (uint64_t value = 1; value <= limit; ++value) {
        sum += value;
        check = (check ^ value) * 0x100000001b3ULL;
    }
    saved_if = kernel_preempt_section_enter();
    job->BatchResults[slot][0] = xis_native_index();
    job->BatchResults[slot][1] = sum;
    job->BatchResults[slot][2] = check;
    kernel_preempt_section_leave(saved_if);
}

// ====== event_private ======

static KERNEL_EVENT_REPLY event_private(uint32_t reason, uint64_t token)
{
    KERNEL_EVENT_REPLY reply = x86_scheduler_event_request(reason,XCN_EVENT,token);
    if (reply.reply.status != KERNEL_SCHEDULER_OK) kernel_scheduler_fail_stop();
    return reply;
}

// ====== run_batch_private ======

static int run_batch_private(uint32_t cpu, uint64_t argument, uint32_t count, uint64_t result[4])
{
    uint64_t waits = 0;
    if (!result ||
        !count ||
        count > X86_REMOTE_BATCH_MAX ||
        x86_scheduler_index() != 2 ||
        !x86_scheduler_driver_task_valid() ||
        !argument ||
        argument > KERNEL_SHELL_AP_MAX
    )
        return 0;
    uint64_t saved_if = kernel_preempt_section_enter();
    int requested = x86_remote_request_batch(cpu,count,job_private,argument);
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

// ====== kernel_shell_remote_on ======

int kernel_shell_remote_on(uint32_t cpu, uint64_t argument, uint64_t result[4])
{
    return run_batch_private(cpu,argument,1,result);
}

// ====== kernel_shell_remote ======

int kernel_shell_remote(uint64_t argument, uint64_t result[4]) {
    return kernel_shell_remote_on(1,argument,result);
}

// ====== kernel_shell_remote_run ======

int kernel_shell_remote_run(uint32_t cpu, uint64_t argument, uint64_t result[6]) {
    return result && kernel_shell_remote_on(cpu,argument,result) &&
        x86_remote_statistics(&result[4]);
}

// ====== kernel_shell_remote_batch ======

int kernel_shell_remote_batch(uint32_t cpu, uint64_t argument, uint32_t count, uint64_t result[8]) {
    return result && run_batch_private(cpu,argument,count,result) &&
        x86_remote_statistics(&result[4]) && x86_remote_batch_info(&result[6]);
}
