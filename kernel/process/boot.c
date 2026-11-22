#include "boot.h"
#include "arch/x86/kernel/descriptors/protection.h"
#include "arch/x86/kernel/process/manager.h"
#include "kernel/diagnostics/log.h"

extern const unsigned char kernel_user_init[];
extern const BOOT_U64 kernel_user_init_length;

// ====== kernel_process_boot ======

int kernel_process_boot(X86_PAGE_WINDOW *window, X86_LAPIC_TIMER *timer)
{
    X86_PROCESS_IMAGE image = {kernel_user_init, kernel_user_init_length};
    uint64_t pid = 0;
    uint32_t state = 0;
    KERNEL_PROCESS_INFO info = {0};
    if (!x86_user_mode_allowed()) return kernel_log(2, "PROCESS_DISABLED");
    if (!x86_process_runtime_start(window, timer) || !x86_process_manager_init()) return 0;
    if (!x86_process_spawn(&image, 0, &pid)) return 0;
    int stepped = 1;
    while (stepped && state != KERNEL_PROC_ZOMBIE)
        stepped = x86_process_step(pid, 0, &state);
    int reaped = stepped && x86_process_reap(pid, &info);
    return reaped && info.ExitKind == KERNEL_PROC_EXIT_NORMAL && info.ExitCode == 0 &&
        kernel_log(0, "KERNEL_PROCESS_OK");
}
