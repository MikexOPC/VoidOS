#include "percpu.h"
#include "kernel/diagnostics/log.h"

// ====== x86_percpu_bsp_report ======

int x86_percpu_bsp_report(void) {
    const X86_CPU_LOCAL *owner = x86_percpu_bsp();
    if (!owner || kernel_lock_current_context() != &owner->Locks || owner->Locks.Depth) return 0;
    return kernel_text("KERNEL_PERCPU_BASE: ") && kernel_number(owner->Self) &&
        kernel_text("\nKERNEL_PERCPU_CONTEXT: ") && kernel_number((uintptr_t)&owner->Locks) &&
        kernel_text("\nKERNEL_PERCPU_EARLY: ") && kernel_number(owner->EarlyContext) &&
        kernel_text("\nKERNEL_PERCPU_INDEX: ") && kernel_number(owner->Index) && kernel_text("\n") &&
        kernel_log(0, "KERNEL_PERCPU_EARLY_RETIRED") && kernel_log(0, "KERNEL_PERCPU_BSP_READY");
}
