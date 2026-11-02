#include "barrier.h"
#include "arch/x86/kernel/cpu/percpu/panic/native/runtime.h"
_Alignas(8) uint64_t xp_stress_arrived, xp_stress_failed;

// ====== failed_private ======

__declspec(noreturn) static void failed_private(void) {
    ks_atomic_store_release(&xp_stress_failed, 1);
    xp_panic_quiet_stop(UINT64_C(0x4b33424152524641));
}

// ====== xp_stress_before_elect ======

void xp_stress_before_elect(uint32_t index) {
    uint32_t start, now, mask = xp_panic_native.Source.Mask;
    if (index >= 4 || xp_panic_native.Count != 4 || (mask != 0xffffffU && mask != UINT32_MAX) ||
        !xp_panic_hw_now(&xp_panic_native.Source, &start)) failed_private();
    uint64_t bit = 1ULL << index;
    int published = 0;
    for (uint32_t poll = 0; poll < 1000000; ++poll) {
        if (ks_atomic_load_acquire(&xp_stress_failed) ||
            !xp_panic_hw_now(&xp_panic_native.Source, &now) ||
            ((now - start) & mask) >= KERNEL_PM_TIMER_HZ) failed_private();
        uint64_t arrived = ks_atomic_load_acquire(&xp_stress_arrived);
        if (!published) {
            if (arrived & bit) failed_private();
            published = ks_atomic_compare(&xp_stress_arrived, arrived, arrived | bit);
        }
        if (published && ks_atomic_load_acquire(&xp_stress_arrived) == 15 &&
            !ks_atomic_load_acquire(&xp_stress_failed)) return;
        ks_pause();
    }
    failed_private();
}
