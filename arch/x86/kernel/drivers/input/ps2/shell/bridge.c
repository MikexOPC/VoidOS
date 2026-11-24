#include "bridge.h"
#include "arch/x86/kernel/descriptors/protection.h"
#include "arch/x86/kernel/interrupts/return/table.h"
#include "arch/x86/kernel/process/manager.h"
#include "arch/x86/kernel/scheduler/integration/control.h"
#include "kernel/diagnostics/log.h"
#include <intrin.h>
#ifdef KERNEL_AP_TASKS
#include "arch/x86/kernel/scheduler/remote/remote.h"
#include "arch/x86/kernel/drivers/video/framebuffer/console/wake/notify.h"
#include "arch/x86/kernel/cpu/percpu/service/native/native.h"
#endif

static X86_PAGE_WINDOW *owner;
static BOOT_U64 budget;
static uint64_t bridge_pid;
static uint64_t bridge_kernel_handle;
static int bridge_failed;
static int bridge_pending, bridge_ready;

static uint32_t gui_phase;
static uint64_t gui_pid;
static int gui_ready;
static BOOT_U64 gui_free_before;

static uint32_t proc_phase;
static int proc_ready;
static int proc_ap_offloaded;
static uint64_t proc_pids[5];
static uint32_t proc_spawned;
static BOOT_U64 proc_free_before;
static KERNEL_PROCESS_STATS proc_peak;
static uint64_t proc_result[6];

#ifdef KERNEL_AP_TASKS
// ====== ap_user_server_job ======

static void ap_user_server_job(uint32_t index)
{
    uint64_t saved_if = kernel_preempt_section_enter();
    X86_REMOTE *job = x86_remote_job();
    if (!job || job->Indices[0] != index || !job->Argument)
        xis_native_abort(0x4b34415055534552ULL);
    uint64_t pid = job->Argument;
    uint32_t cpu = xis_native_index();
    kernel_preempt_section_leave(saved_if);
    uint32_t state = 0;
    uint64_t steps = 0;
    for (uint32_t attempt = 0; attempt < 4000000U; ++attempt) {
        if (!x86_process_step(pid, 0, &state))
            break;
        ++steps;
        if (state == KERNEL_PROC_ZOMBIE)
            break;
        if (state == KERNEL_PROC_BLOCKED)
            _mm_pause();
    }
    saved_if = kernel_preempt_section_enter();
    job->BatchResults[0][0] = cpu;
    job->BatchResults[0][1] = state;
    job->BatchResults[0][2] = steps;
    kernel_preempt_section_leave(saved_if);
}

// ====== ap_wait_event_private ======

static KERNEL_EVENT_REPLY ap_wait_event_private(uint32_t reason, uint64_t token)
{
    KERNEL_EVENT_REPLY reply = x86_scheduler_event_request(reason, XCN_EVENT, token);
    if (reply.reply.status != KERNEL_SCHEDULER_OK) kernel_scheduler_fail_stop();
    return reply;
}
#endif

// ====== bridge_dispatch_private ======

static void bridge_dispatch_private(void)
{
    const unsigned char request[2] = {1, 1};
    unsigned char response[2] = {0};
    KERNEL_PROCESS_TABLE *table = x86_process_manager_table();
    uint32_t wake_task = KERNEL_TASK_NONE;
    uint32_t state = 0;
    if (!table || !bridge_pid || bridge_failed) {
        bridge_ready = 0;
        return;
    }
    int passed = kernel_ipc_send(table, KERNEL_PID_KERNEL, bridge_kernel_handle, request, 2, &wake_task) == 2;
    int renew = 1;
    if (passed) {
        do {
            passed = x86_process_step(bridge_pid, renew, &state);
            renew = 0;
        } while (passed && state == KERNEL_PROC_RUNNABLE);
    }
    passed = passed && state == KERNEL_PROC_BLOCKED &&
        kernel_ipc_peek(table, KERNEL_PID_KERNEL, bridge_kernel_handle, response, 2) == 2 &&
        !kernel_ipc_consume(table, KERNEL_PID_KERNEL, bridge_kernel_handle) &&
        kernel_ipc_receive_ready(table, KERNEL_PID_KERNEL, bridge_kernel_handle) == KERNEL_IPC_WOULD_BLOCK &&
        response[0] == 1 && response[1] == 0;
    if (!passed) {
        (void)kernel_ipc_close_handle(table, KERNEL_PID_KERNEL, bridge_kernel_handle, &wake_task);
        bridge_kernel_handle = 0;
        (void)x86_process_kill(bridge_pid);
        bridge_failed = 1;
    }
    bridge_ready = passed;
}

// ====== proc_spawn_private ======

static void proc_spawn_private(void)
{
    uint64_t h_srv[2] = {0}, h_cli[2] = {0};
    proc_spawned = 0;
    proc_free_before = 0;
    proc_peak = (KERNEL_PROCESS_STATS){0};
    for (uint32_t index = 0; index < 5U; ++index) proc_pids[index] = 0;
    for (uint32_t index = 0; index < 6U; ++index) proc_result[index] = 0;
    int ok = x86_process_manager_ready() && kernel_physical_budget_read(owner->Pool, &proc_free_before);
    for (uint32_t index = 0; ok && index < 5U; ++index) {
        const char *path = (index == 0 || index == 2) ? "/bin/bridge.elf" : "/bin/worker.elf";
        if (!x86_process_spawn_vfs(path, KERNEL_PID_KERNEL, 0, NULL, &proc_pids[index])) {
            ok = 0;
            break;
        }
        ++proc_spawned;
    }
    if (ok) {
        ok = x86_process_channel_create(proc_pids[0], proc_pids[1], &h_srv[0], &h_cli[0]) &&
            x86_process_channel_create(proc_pids[2], proc_pids[3], &h_srv[1], &h_cli[1]) &&
            x86_process_set_arg(proc_pids[0], h_srv[0]) &&
            x86_process_set_arg(proc_pids[1], h_cli[0]) &&
            x86_process_set_arg(proc_pids[2], h_srv[1]) &&
            x86_process_set_arg(proc_pids[3], h_cli[1]) &&
            x86_process_set_arg(proc_pids[4], 0) &&
            x86_process_stats(&proc_peak);
    }
    proc_ready = ok;
}

// ====== proc_step_private ======

static void proc_step_private(void)
{
    uint32_t first = proc_ap_offloaded ? 1U : 0U;
    uint32_t max_rounds = proc_ap_offloaded ? 4096U : 32U;
    int ok = proc_ready;
    for (uint32_t round = 0; ok && round < max_rounds; ++round) {
        uint32_t alive = 0;
        for (uint32_t index = first; index < 5U; ++index) {
            uint32_t state = 0;
            if (!x86_process_step(proc_pids[index], 0, &state)) {
                ok = 0;
                break;
            }
            if (state != KERNEL_PROC_ZOMBIE)
                ++alive;
        }
        if (!alive)
            break;
        if (proc_ap_offloaded)
            _mm_pause();
    }
    proc_ready = ok;
}

// ====== proc_reap_private ======

static void proc_reap_private(void)
{
    KERNEL_PROCESS_STATS after = {0};
    uint32_t reaped = 0, ipc_ok = 0;
    uint64_t fault_vector = 0;
    int ok = proc_ready;
    for (uint32_t index = 0; index < proc_spawned; ++index) {
        KERNEL_PROCESS_INFO info = {0};
        if (x86_process_query(proc_pids[index], &info) && info.State != KERNEL_PROC_ZOMBIE)
            (void)x86_process_kill(proc_pids[index]);
        if (x86_process_reap(proc_pids[index], &info)) {
            ++reaped;
            if (index < 4U && info.ExitKind == KERNEL_PROC_EXIT_NORMAL && info.ExitCode == 0)
                ++ipc_ok;
            if (index == 4U && info.ExitKind == KERNEL_PROC_EXIT_FAULT)
                fault_vector = info.FaultVector;
        } else {
            ok = 0;
        }
    }
    ok = ok && proc_spawned == 5U && reaped == 5U && ipc_ok == 4U && fault_vector == 6ULL &&
        proc_peak.ActiveProcesses >= 6U &&
        x86_process_stats(&after) && after.ActiveProcesses == 1U &&
        after.ZombieProcesses == 0U && after.ChannelCount == (bridge_failed ? 0U : 1U) &&
        kernel_physical_budget_matches(owner->Pool, proc_free_before);
    proc_result[0] = proc_spawned;
    proc_result[1] = proc_peak.ActiveProcesses;
    proc_result[2] = ipc_ok;
    proc_result[3] = fault_vector;
    proc_result[4] = reaped;
    proc_ready = ok;
}

// ====== x86_shell_bridge_open ======

int x86_shell_bridge_open(X86_PAGE_WINDOW *window, X86_LAPIC_TIMER *timer)
{
    uint64_t user_handle = 0;
    if (!x86_user_mode_allowed()) return 1;
    if (owner || !x86_process_runtime_start(window, timer) || !x86_process_manager_init() ||
        !kernel_physical_budget_read(window->Pool, &budget)) return 0;
    if (!x86_process_spawn_vfs("/bin/bridge.elf", KERNEL_PID_KERNEL, KERNEL_PROC_FLAG_RESIDENT, NULL, &bridge_pid))
        return 0;
    if (!x86_process_channel_create(KERNEL_PID_KERNEL, bridge_pid, &bridge_kernel_handle, &user_handle) ||
        !x86_process_set_arg(bridge_pid, user_handle)) {
        uint32_t wake = KERNEL_TASK_NONE;
        if (bridge_kernel_handle)
            (void)kernel_ipc_close_handle(x86_process_manager_table(), KERNEL_PID_KERNEL, bridge_kernel_handle, &wake);
        (void)x86_process_kill(bridge_pid);
        (void)x86_process_reap(bridge_pid, NULL);
        bridge_pid = 0;
        bridge_kernel_handle = 0;
        return 0;
    }
    owner = window;
    bridge_failed = 0;
    return 1;
}

// ====== kernel_shell_bridge_status ======

int kernel_shell_bridge_status(void)
{
    if (!owner || bridge_pending || proc_phase || x86_scheduler_index() != 2 ||
        !x86_scheduler_driver_task_valid()) return 0;
    bridge_pending = 1;
    x86_scheduler_yield();
    if (bridge_pending) kernel_scheduler_fail_stop();
    return bridge_ready;
}

// ====== kernel_shell_proc_run ======

int kernel_shell_proc_run(uint64_t result[6])
{
    if (!owner || !result || bridge_pending || proc_phase || x86_scheduler_index() != 2 ||
        !x86_scheduler_driver_task_valid()) return 0;
    proc_ap_offloaded = 0;
    proc_phase = 1;
    x86_scheduler_yield();
    if (proc_phase) kernel_scheduler_fail_stop();

    uint64_t ap_cpu = 0;
#ifdef KERNEL_AP_TASKS
    if (proc_ready && xis_native.Count >= 2) {
        uint64_t saved_if = kernel_preempt_section_enter();
        int requested = x86_remote_request(1, ap_user_server_job, proc_pids[0]);
        kernel_preempt_section_leave(saved_if);
        if (requested) {
            proc_ap_offloaded = 1;
            // Yield once so BSP dispatcher launches the remote AP1 task before BSP steps client1.
            x86_scheduler_yield();
        }
    }
#endif

    if (proc_ready) {
        proc_phase = 2;
        x86_scheduler_yield();
        if (proc_phase) kernel_scheduler_fail_stop();
    }

#ifdef KERNEL_AP_TASKS
    if (proc_ap_offloaded) {
        uint64_t ap_out[4] = {0};
        int taken = 0;
        for (uint32_t round = 0; round < 4096U; ++round) {
            KERNEL_EVENT_REPLY observed = ap_wait_event_private(X86_TASK_OBSERVE, 0);
            taken = x86_remote_take(ap_out);
            if (taken) break;
            (void)ap_wait_event_private(X86_TASK_WAIT, observed.epoch);
        }
        if (taken != 1 || ap_out[0] != 1ULL || ap_out[1] != KERNEL_PROC_ZOMBIE)
            proc_ready = 0;
        else
            ap_cpu = ap_out[0];
    }
#endif

    proc_phase = 3;
    x86_scheduler_yield();
    if (proc_phase) kernel_scheduler_fail_stop();
    proc_result[5] = ap_cpu;
    for (uint32_t index = 0; index < 6U; ++index)
        result[index] = proc_result[index];
    return proc_ready;
}

// ====== gui_dispatch_private ======

static void gui_dispatch_private(void)
{
    if (gui_phase == 1U) {
        gui_ready = kernel_physical_budget_read(owner->Pool, &gui_free_before)
            && x86_process_spawn_vfs("/bin/ui.elf", KERNEL_PID_KERNEL, 0, NULL, &gui_pid);
        if (gui_ready)
            gui_ready = x86_process_set_arg(gui_pid, 0);
    }
    else if (gui_phase == 2U) {
        uint32_t state = 0;
        if (!x86_process_step(gui_pid, 1, &state))
            gui_ready = 0;
        else if (state == KERNEL_PROC_ZOMBIE)
            gui_ready = 0;
        else if (!x86_process_step_children_budget(gui_pid, 1))
            gui_ready = 0;
    }
    else if (gui_phase == 3U) {
        KERNEL_PROCESS_TABLE *table = x86_process_manager_table();
        uint64_t child = 0;
        while (kernel_process_list_children(table, gui_pid, &child, 1U)) {
            KERNEL_PROCESS_INFO info = {0};
            if (!x86_process_query(child, &info) ||
                (info.State != KERNEL_PROC_ZOMBIE && !x86_process_kill(child)) ||
                !x86_process_reap(child, NULL)
            )
                kernel_panic("GUI_CHILD_CLEANUP_FAILED");
        }
        KERNEL_PROCESS_INFO info = {0};
        if (gui_pid && x86_process_query(gui_pid, &info)) {
            if (info.State != KERNEL_PROC_ZOMBIE)
                (void)x86_process_kill(gui_pid);
            gui_ready = x86_process_reap(gui_pid, &info)
                && info.ExitKind == KERNEL_PROC_EXIT_NORMAL && info.ExitCode == 0;
        }
        gui_pid = 0;
        gui_ready = gui_ready && kernel_physical_budget_matches(owner->Pool, gui_free_before);
    }
    gui_phase = 0;
}

// ====== kernel_shell_gui_run ======

int kernel_shell_gui_run(void)
{
    if (!owner ||
        bridge_pending ||
        proc_phase ||
        gui_phase ||
        gui_pid ||
        x86_scheduler_index() != 2 ||
        !x86_scheduler_driver_task_valid()
    )
        return 0;
    gui_phase = 1;
    x86_scheduler_yield();
    while (gui_ready) {
        gui_phase = 2;
        x86_scheduler_yield();
    }
    gui_phase = 3;
    x86_scheduler_yield();
    return gui_ready;
}

// ====== x86_shell_bridge_dispatch ======

void x86_shell_bridge_dispatch(void)
{
    if (!bridge_pending && !proc_phase && !gui_phase) return;
    if (!owner || !x86_irq_ready() || !x86_irq_registry()) kernel_scheduler_fail_stop();
    if (gui_phase)
        gui_dispatch_private();
    if (bridge_pending) {
        bridge_dispatch_private();
        bridge_pending = 0;
    }
    if (proc_phase == 1U) {
        proc_spawn_private();
        proc_phase = 0;
    } else if (proc_phase == 2U) {
        proc_step_private();
        proc_phase = 0;
    } else if (proc_phase == 3U) {
        proc_reap_private();
        proc_phase = 0;
    }
    if (!x86_irq_ready()) kernel_scheduler_fail_stop();
}

// ====== x86_shell_bridge_close ======

int x86_shell_bridge_close(void)
{
    if (!owner) return 1;
    if (bridge_pending ||
        proc_phase ||
        gui_phase ||
        gui_pid
    ) return 0;
    uint32_t wake_task = KERNEL_TASK_NONE;
    if (bridge_kernel_handle) {
        (void)kernel_ipc_close_handle(x86_process_manager_table(), KERNEL_PID_KERNEL, bridge_kernel_handle, &wake_task);
        bridge_kernel_handle = 0;
    }
    if (bridge_pid) {
        KERNEL_PROCESS_INFO info = {0};
        if (x86_process_query(bridge_pid, &info) && info.State != KERNEL_PROC_ZOMBIE)
            (void)x86_process_kill(bridge_pid);
        if (!x86_process_reap(bridge_pid, NULL)) return 0;
        bridge_pid = 0;
    }
    if (!kernel_physical_budget_matches(owner->Pool, budget)) return 0;
    owner = 0;
    bridge_ready = 0;
    bridge_failed = 0;
    proc_ready = 0;
    return 1;
}
