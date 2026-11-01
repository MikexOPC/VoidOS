#ifndef OS_X86_PERCPU_CLOCK_H
#define OS_X86_PERCPU_CLOCK_H
#include "../percpu.h"
#include "arch/x86/kernel/timer/lapic/timer.h"

// ====== per_cpu_wait_contract ======

int x86_percpu_wait_bind(const X86_PM_CLOCK *source, uint32_t ticks, uint32_t max_polls);
const KERNEL_LOCK_WAIT *x86_percpu_wait_current(void);
int x86_percpu_wait_initialize(const KERNEL_BOOT_CONTEXT *context, X86_PAGE_WINDOW *window,
    const KERNEL_ACPI_DIRECTORY *directory);
int x86_percpu_wait_report(void);
int x86_percpu_wait_ap_prepare(uint32_t index);
int x86_percpu_wait_ap_valid(uint32_t index);
int x86_percpu_wait_ap_cancel(uint32_t index);
#endif
