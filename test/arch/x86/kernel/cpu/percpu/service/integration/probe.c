#include "probe.h"
#include "arch/x86/kernel/cpu/percpu/service/native/native.h"
#include "kernel/diagnostics/log.h"
#include "arch/x86/kernel/stack/main.h"

// ====== number_private ======

static void number_private(const char *name, uint64_t value) {
    if (!kernel_text(name) || !kernel_number(value) || !kernel_text("\n")) xis_native_abort(0x4b344950494c4f47ULL);
}

// ====== xis_service_probe ======

__declspec(noreturn) void xis_service_probe(void) {
    if (ks_irq_state() || xis_native_index() != 0 || xis_native.Count != 4) xis_native_abort(0x4b34495049425350ULL);
    number_private("KERNEL_IPI_NATIVE: ", (uint64_t)&xis_native);
    number_private("KERNEL_IPI_IDTR: ", (uint64_t)&xis_native.Idtr);
    for (uint32_t index = 1; index < 4; ++index)
        if (!xis_native_wait(index, 0, XIS_READY)) xis_native_abort(0x4b34495049524459ULL);
#if KERNEL_TEST_AP_SERVICE == 5
    if (xis_publish(1, 0) != 1 || !xis_native_send(1) || !xis_native_wait(1, 2, 1))
        xis_native_abort(0x4b34495049484e44ULL);
    xis_native_abort(0x4b344950494e4d35ULL);
#else
    for (uint32_t round = 1; round <= 100; ++round) {
        for (uint32_t index = 1; index < 4; ++index) {
            uint64_t generation = xis_publish(index, 0);
            if (generation != round || !xis_native_send(index)) xis_native_abort(0x4b3449504953454eULL);
            XIS_WAIT wait = {xis_native_now, NULL, xp_panic_native.Source.Mask, KERNEL_PM_TIMER_HZ, 1000000, 0};
            uint32_t result = xis_wait(index, generation, &wait);
            if (result != XIS_WAIT_ACK) {
                number_private("KERNEL_IPI_FAILED_PHASE: ", xis_value(index, XIS_PHASE));
                xis_native_abort(0x4b34495049544d4fULL);
            }
            if (!xis_native_wait(index, 1, generation)) xis_native_abort(0x4b34495049565246ULL);
        }
    }
    for (uint32_t index = 1; index < 4; ++index) {
        if (xis_value(index, XIS_ACK) != 100 || ks_atomic_load_acquire(&xis_native.Cpus[index].Delivered) != 100 ||
            ks_atomic_load_acquire(&xis_native.Cpus[index].Eois) != 100 ||
            !ks_atomic_load_acquire(&xis_native.Cpus[index].Spurious)) xis_native_abort(0x4b34495049454e44ULL);
        number_private("KERNEL_IPI_ACK: ", xis_value(index, XIS_ACK));
    }
    if (!kernel_log(0, "KERNEL_IPI_SERVICE_READY")) xis_native_abort(0x4b344950494c4f47ULL);
#if KERNEL_TEST_AP_SERVICE == 4
    xis_native_abort(0x4b344950494e4d34ULL);
#else
    x86_kernel_stop(0x4b34495049504153ULL);
#endif
#endif
}
