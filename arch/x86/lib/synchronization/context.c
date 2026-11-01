#include "kernel/sync/internal.h"
#include "arch/x86/kernel/cpu/percpu/percpu.h"
static KERNEL_LOCK_CONTEXT bootstrap_context;
static uint32_t bootstrap_phase;
uint32_t ks_boot_cpu(void);

// ====== kernel_lock_bootstrap_initialize ======

int kernel_lock_bootstrap_initialize(void) {
    uint64_t saved_if = ks_irq_save();
    int ready = 0;
    if (ks_boot_cpu() && !bootstrap_phase && kernel_lock_context_initialize(&bootstrap_context, NULL)) {
        bootstrap_phase = 1; ready = 1;
    }
    ks_irq_restore(saved_if);
    return ready;
}

// ====== kernel_lock_current_context ======

KERNEL_LOCK_CONTEXT *kernel_lock_current_context(void) {
    uint64_t saved_if = ks_irq_save();
    KERNEL_LOCK_CONTEXT *context = NULL;
    if (ks_boot_cpu()) {
        if (bootstrap_phase == 1 && ks_context_valid(&bootstrap_context)) context = &bootstrap_context;
        else if (bootstrap_phase == 2) context = x86_percpu_current_context();
    } else context = x86_percpu_current_context();
    ks_irq_restore(saved_if);
    return context;
}

// ====== kernel_lock_bootstrap_promote ======

int kernel_lock_bootstrap_promote(void) {
    uint64_t saved_if = ks_irq_save();
    int ready = 0;
    if (ks_boot_cpu() && bootstrap_phase == 1 && x86_percpu_bsp_promote(&bootstrap_context)) {
        bootstrap_phase = 2; ready = 1;
    }
    ks_irq_restore(saved_if);
    return ready;
}
