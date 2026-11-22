#include "bridge.h"
#include "arch/x86/kernel/descriptors/protection.h"
#include "arch/x86/kernel/interrupts/return/table.h"
#include "arch/x86/kernel/process/manager.h"
#include "arch/x86/kernel/scheduler/integration/control.h"
#include "kernel/diagnostics/log.h"

extern const unsigned char kernel_user_bridge[];
extern const BOOT_U64 kernel_user_bridge_length;
extern const unsigned char kernel_user_worker[];
extern const BOOT_U64 kernel_user_worker_length;

static X86_PAGE_WINDOW *owner;
static BOOT_U64 budget;
static uint64_t bridge_pid;
static uint64_t bridge_kernel_handle;
static int bridge_failed;
static int bridge_pending, bridge_ready;
static int proc_pending, proc_ready;
static uint64_t proc_result[5];

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

// ====== proc_dispatch_private ======

static void proc_dispatch_private(void)
{
    const X86_PROCESS_IMAGE server_img = {kernel_user_bridge, kernel_user_bridge_length};
    const X86_PROCESS_IMAGE worker_img = {kernel_user_worker, kernel_user_worker_length};
    uint64_t pids[5] = {0};
    uint64_t h_srv[2] = {0}, h_cli[2] = {0};
    BOOT_U64 free_before = 0;
    KERNEL_PROCESS_STATS peak = {0}, after = {0};
    uint32_t spawned = 0, reaped = 0, ipc_ok = 0;
    uint64_t fault_vector = 0;
    int ok = x86_process_manager_ready() && kernel_physical_budget_read(owner->Pool, &free_before);

    for (uint32_t index = 0; ok && index < 5U; ++index) {
        const X86_PROCESS_IMAGE *img = (index == 0 || index == 2) ? &server_img : &worker_img;
        if (!x86_process_spawn(img, 0, &pids[index])) {
            ok = 0;
            break;
        }
        ++spawned;
    }
    if (ok) {
        ok = x86_process_channel_create(pids[0], pids[1], &h_srv[0], &h_cli[0]) &&
            x86_process_channel_create(pids[2], pids[3], &h_srv[1], &h_cli[1]) &&
            x86_process_set_arg(pids[0], h_srv[0]) &&
            x86_process_set_arg(pids[1], h_cli[0]) &&
            x86_process_set_arg(pids[2], h_srv[1]) &&
            x86_process_set_arg(pids[3], h_cli[1]) &&
            x86_process_set_arg(pids[4], 0) &&
            x86_process_stats(&peak);
    }
    for (uint32_t round = 0; ok && round < 32U; ++round) {
        uint32_t alive = 0;
        for (uint32_t index = 0; index < 5U; ++index) {
            uint32_t state = 0;
            if (!x86_process_step(pids[index], 0, &state)) {
                ok = 0;
                break;
            }
            if (state != KERNEL_PROC_ZOMBIE)
                ++alive;
        }
        if (!alive)
            break;
    }
    for (uint32_t index = 0; index < spawned; ++index) {
        KERNEL_PROCESS_INFO info = {0};
        if (x86_process_query(pids[index], &info) && info.State != KERNEL_PROC_ZOMBIE)
            (void)x86_process_kill(pids[index]);
        if (x86_process_reap(pids[index], &info)) {
            ++reaped;
            if (index < 4U && info.ExitKind == KERNEL_PROC_EXIT_NORMAL && info.ExitCode == 0)
                ++ipc_ok;
            if (index == 4U && info.ExitKind == KERNEL_PROC_EXIT_FAULT)
                fault_vector = info.FaultVector;
        } else {
            ok = 0;
        }
    }
    ok = ok && spawned == 5U && reaped == 5U && ipc_ok == 4U && fault_vector == 6ULL &&
        peak.ActiveProcesses >= 6U &&
        x86_process_stats(&after) && after.ActiveProcesses == 1U &&
        after.ZombieProcesses == 0U && after.ChannelCount == (bridge_failed ? 0U : 1U) &&
        kernel_physical_budget_matches(owner->Pool, free_before);

    proc_result[0] = spawned;
    proc_result[1] = peak.ActiveProcesses;
    proc_result[2] = ipc_ok;
    proc_result[3] = fault_vector;
    proc_result[4] = reaped;
    proc_ready = ok;
}

// ====== x86_shell_bridge_open ======

int x86_shell_bridge_open(X86_PAGE_WINDOW *window, X86_LAPIC_TIMER *timer)
{
    const X86_PROCESS_IMAGE image = {kernel_user_bridge, kernel_user_bridge_length};
    uint64_t user_handle = 0;
    if (!x86_user_mode_allowed()) return 1;
    if (owner || !x86_process_runtime_start(window, timer) || !x86_process_manager_init() ||
        !kernel_physical_budget_read(window->Pool, &budget)) return 0;
    if (!x86_process_spawn(&image, KERNEL_PROC_FLAG_RESIDENT, &bridge_pid))
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
    if (!owner || bridge_pending || proc_pending || x86_scheduler_index() != 2 ||
        !x86_scheduler_driver_task_valid()) return 0;
    bridge_pending = 1;
    x86_scheduler_yield();
    if (bridge_pending) kernel_scheduler_fail_stop();
    return bridge_ready;
}

// ====== kernel_shell_proc_run ======

int kernel_shell_proc_run(uint64_t result[5])
{
    if (!owner || !result || bridge_pending || proc_pending || x86_scheduler_index() != 2 ||
        !x86_scheduler_driver_task_valid()) return 0;
    proc_pending = 1;
    x86_scheduler_yield();
    if (proc_pending) kernel_scheduler_fail_stop();
    for (uint32_t index = 0; index < 5U; ++index)
        result[index] = proc_result[index];
    return proc_ready;
}

// ====== x86_shell_bridge_dispatch ======

void x86_shell_bridge_dispatch(void)
{
    if (!bridge_pending && !proc_pending) return;
    if (!owner || !x86_irq_ready() || !x86_irq_registry()) kernel_scheduler_fail_stop();
    if (bridge_pending) {
        bridge_dispatch_private();
        bridge_pending = 0;
    }
    if (proc_pending) {
        proc_dispatch_private();
        proc_pending = 0;
    }
    if (!x86_irq_ready()) kernel_scheduler_fail_stop();
}

// ====== x86_shell_bridge_close ======

int x86_shell_bridge_close(void)
{
    if (!owner) return 1;
    if (bridge_pending || proc_pending) return 0;
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
