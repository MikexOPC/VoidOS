#include "group.h"
#include "arch/x86/kernel/cpu/percpu/internal.h"
#include "arch/x86/kernel/descriptors/stacks.h"
#include "kernel/diagnostics/log.h"

// ====== number_private ======

static int number_private(const char *name, uint64_t value) {
    return kernel_text(name) && kernel_number(value) && kernel_text("\n");
}

// ====== elapsed_private ======

static int elapsed_private(XN_LAUNCH *m, uint32_t start, uint32_t ticks) {
    uint32_t now;
    return kernel_clock_now(&m->Clock, &now) && ((now - start) & m->Clock.Mask) < ticks;
}

// ====== xp_group_probe ======

int xp_group_probe(XN_LAUNCH *m, uint32_t origin) {
    if (!xn_safe(m) || m->State != 4 || m->Group->Header.Count != 4 ||
        (origin != 0 && origin != 3) || xs_snapshot() != (m->Token | XS_PARKED) ||
        xp_panic_value(XP_PANIC_ONLINE) != 15 || xp_panic_value(XP_PANIC_KNOWN) != 15 ||
        xp_panic_value(XP_PANIC_OWNER) || xp_panic_value(XP_PANIC_PHASE)) return 0;
    for (uint32_t index = 1; index < 4; ++index) if (x86_percpu_ap_phase(index) != 2) return 0;
    if (!number_private("KERNEL_GROUP_PANIC_ORIGIN: ", origin) ||
        !number_private("KERNEL_GROUP_BSP_MAIN_BASE: ", X86_KERNEL_MAIN_BASE) ||
        !number_private("KERNEL_GROUP_BSP_MAIN_TOP: ", X86_KERNEL_MAIN_TOP) ||
        !number_private("KERNEL_GROUP_BSP_IDTR: ", (uint64_t)&xp_panic_native.BspIdtr) ||
        !kernel_log(1, "KERNEL_AP_GROUP_PANIC_STARTING")) return 0;
    if (!origin) xp_panic_voluntary(UINT64_C(0x4752504253500001));
    uint32_t start, ticks = kernel_clock_us(&m->Clock, 1000000);
    if (!ticks || ticks >= m->Clock.Mask / 2 || !kernel_clock_now(&m->Clock, &start)) return 0;
    for (uint32_t polls = 0; polls < 1000000; ++polls) {
        uint32_t icr;
        if (!elapsed_private(m, start, ticks) || !xp_panic_hw_read(NULL, 0x300, &icr)) return 0;
        if (!(icr & 0x1000)) {
            if (!xn_safe(m)) return 0;
            // Intel SDM 253668-078US 11.6.1: physical NMI, high ID[31:24], low=4400h.
            if (!xp_panic_hw_write(NULL, 0x310, m->Group->Cpus[origin].ApicId << 24) ||
                !xp_panic_hw_write(NULL, 0x300, 0x4400)) return 0;
            for (uint32_t wait = 0; wait < 1000000; ++wait) {
                if (!elapsed_private(m, start, ticks)) xp_panic_voluntary(UINT64_C(0x47525054494d454f));
                ks_pause();
            }
            xp_panic_voluntary(UINT64_C(0x475250504f4c4c53));
        }
        ks_pause();
    }
    return 0;
}
