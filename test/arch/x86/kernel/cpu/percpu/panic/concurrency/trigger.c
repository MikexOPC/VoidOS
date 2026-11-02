#include "barrier.h"
#include "arch/x86/kernel/cpu/percpu/panic/native/runtime.h"
#include "kernel/diagnostics/log.h"

// ====== failed_private ======

__declspec(noreturn) static void failed_private(void) {
    xp_panic_quiet_stop(UINT64_C(0x4b33495049464149));
}

// ====== xp_stress_start ======

__declspec(noreturn) void xp_stress_start(void) {
    KERNEL_LOCK_CONTEXT *locks = kernel_lock_current_context();
    XP_PANIC_NATIVE *n = &xp_panic_native;
    if (!ks_boot_cpu() || ks_irq_state() || !locks || locks->Depth || n->Count != 4 ||
        xp_panic_value(XP_PANIC_ONLINE) != 15 || xp_panic_value(XP_PANIC_OWNER) ||
        !xp_panic_lapic_held(n->Lapic) || x86_kernel_root() != n->Root) failed_private();
    for (uint32_t i = 1; i < 4; ++i) if (x86_percpu_ap_phase(i) != 2) failed_private();
    if (!kernel_text("KERNEL_AP_STRESS_BSP_IDTR: ") || !kernel_number((uint64_t)&n->BspIdtr) ||
        !kernel_text("\nKERNEL_AP_STRESS_BSP_GDTR: ") || !kernel_number((uint64_t)&n->BspGdtr) ||
        !kernel_text("\n") || !kernel_log(0, "KERNEL_AP_STRESS_IPI_ARMED")) failed_private();
    uint32_t start, now;
    if (!xp_panic_hw_now(&n->Source, &start)) failed_private();
    uint32_t polls = 0;
    for (uint32_t order = 1; order <= 4; ++order) {
        uint32_t icr, index = order % 4;
        for (;;) {
            if (++polls > 1000000 || !xp_panic_hw_now(&n->Source, &now) ||
                ((now - start) & n->Source.Mask) >= KERNEL_PM_TIMER_HZ ||
                !xp_panic_hw_read(NULL, 0x300, &icr)) failed_private();
            if (!(icr & 0x1000)) break;
            ks_pause();
        }
        // Intel SDM 078US 11.6.1/figure 11-12/table 11-3: physical NMI 4400h; high ID[31:24].
        // APs first, BSP last; explicit destinations bypass external LINT1 routing.
        if (!xp_panic_hw_write(NULL, 0x310, n->Ids[index] << 24) ||
            !xp_panic_hw_write(NULL, 0x300, 0x4400)) failed_private();
    }
    while (++polls <= 1000000 && xp_panic_hw_now(&n->Source, &now) &&
        ((now - start) & n->Source.Mask) < KERNEL_PM_TIMER_HZ) ks_pause();
    failed_private();
}
