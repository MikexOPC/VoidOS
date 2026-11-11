#include "boot.h"
#include "arch/x86/kernel/descriptors/protection.h"
#include "arch/x86/kernel/process/process.h"
#include "kernel/diagnostics/log.h"

extern const unsigned char kernel_user_init[];
extern const BOOT_U64 kernel_user_init_length;

// ====== kernel_process_boot ======

int kernel_process_boot(X86_PAGE_WINDOW *window, X86_LAPIC_TIMER *timer)
{
    X86_PROCESS_IMAGE image = {kernel_user_init, kernel_user_init_length};
    X86_PROCESS_RESULT result;
    if (!x86_user_mode_allowed()) return kernel_log(2, "PROCESS_DISABLED");
    if (!x86_process_open(window, timer)) return 0;
    int ran = x86_process_run(&image, &result);
    int closed = x86_process_close();
    return ran && closed && result.Kind == X86_PROCESS_EXIT && result.Code == 0 && kernel_log(0, "KERNEL_PROCESS_OK");
}
