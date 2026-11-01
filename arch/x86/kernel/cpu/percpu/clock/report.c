#include "clock.h"
#include "kernel/diagnostics/log.h"

// ====== x86_percpu_wait_report ======

int x86_percpu_wait_report(void) {
    const KERNEL_LOCK_WAIT *wait = x86_percpu_wait_current();
    if (!wait) return 0;
    const X86_PM_CLOCK *source = wait->Clock.Context;
    return kernel_text("KERNEL_PERCPU_WAIT: ") && kernel_number((uintptr_t)wait) &&
        kernel_text("\nKERNEL_PERCPU_WAIT_SOURCE: ") && kernel_number((uintptr_t)source) &&
        kernel_text("\nKERNEL_PERCPU_WAIT_PORT: ") && kernel_number(source->Port) &&
        kernel_text("\nKERNEL_PERCPU_WAIT_MASK: ") && kernel_number(source->Mask) &&
        kernel_text("\nKERNEL_PERCPU_WAIT_TICKS: ") && kernel_number(wait->Ticks) &&
        kernel_text("\nKERNEL_PERCPU_WAIT_POLLS: ") && kernel_number(wait->MaxPolls) &&
        kernel_text("\n") && kernel_log(0, "KERNEL_PERCPU_WAIT_READY");
}
