#include "hold.h"
#include "arch/x86/kernel/cpu/percpu/panic/native/runtime.h"
#include "kernel/diagnostics/log.h"
static KERNEL_LOCK lock;

// ====== xp_panic_group_lock_probe ======

__declspec(noreturn) void xp_panic_group_lock_probe(void) {
    KERNEL_LOCK_CONTEXT *context = kernel_lock_current_context();
    KERNEL_LOCK_GUARD guard = {0};
    if (!context || context->Depth || !ks_boot_cpu() || ks_irq_state() ||
        xp_panic_native.Count != 4 || xp_panic_value(XP_PANIC_ONLINE) != 15 ||
        !ks_atomic_load_acquire(&xp_panic_armed) || xp_panic_value(XP_PANIC_OWNER) ||
        !kernel_lock_initialize(&lock, 300) || !kernel_lock_acquire(&lock, context, &guard))
        ks_fail_stop("AP_STRESS_LOCK_SETUP_FAILED");
    if (!kernel_text("KERNEL_AP_STRESS_LOCK: ") || !kernel_number((uint64_t)&lock) ||
        !kernel_text("\nKERNEL_AP_STRESS_GUARD: ") || !kernel_number((uint64_t)&guard) ||
        !kernel_text("\n") || !kernel_log(0, "KERNEL_AP_STRESS_LOCK_HELD"))
        ks_fail_stop("AP_STRESS_LOCK_LOG_FAILED");
    kernel_panic((const char *)(uintptr_t)UINTPTR_MAX);
}
