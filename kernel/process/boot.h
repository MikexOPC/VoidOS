#ifndef OS_KERNEL_PROCESS_BOOT_H
#define OS_KERNEL_PROCESS_BOOT_H
#include "arch/x86/mm/window/page.h"
#include "arch/x86/kernel/timer/lapic/timer.h"

int kernel_process_boot(X86_PAGE_WINDOW *window, X86_LAPIC_TIMER *timer);
#endif
