#include "launch.h"
#include "arch/x86/kernel/cpu/percpu/startup/transport/native/native.h"
#include "arch/x86/kernel/cpu/percpu/internal.h"
#include "arch/x86/kernel/cpu/percpu/startup/private/frame.h"
#include "kernel/diagnostics/log.h"
static XN_LAUNCH launch;

// ====== number_private ======

static int number_private(const char *name, uint64_t value) {
    return kernel_text(name) && kernel_number(value) && kernel_text("\n");
}

// ====== xn_probe ======

int xn_probe(const XJ_GROUP *joint, int send_nmi) {
    if (!xn_prepare(&launch, joint)) return kernel_log(3, "KERNEL_AP_NATIVE_PREFLIGHT_FAILED") && 0;
    if (!number_private("KERNEL_AP_NATIVE_OWNER: ", (uint64_t)&launch) ||
        !kernel_log(1, "KERNEL_AP_NATIVE_STARTING")) return 0;
    int started = xn_run(&launch, send_nmi);
    if (!number_private("KERNEL_AP_NATIVE_ERROR: ", launch.Error) ||
        !number_private("KERNEL_AP_NATIVE_WAKE_ERROR: ", launch.Wake.Error) ||
        !number_private("KERNEL_AP_NATIVE_STATE: ", xs_snapshot()) ||
        !number_private("KERNEL_AP_NATIVE_CONTEXT_PHASE: ", x86_percpu_ap_phase(1)) ||
        !number_private("KERNEL_AP_NATIVE_GATE: ", launch.Gate) ||
        !number_private("KERNEL_AP_NATIVE_NMI: ", launch.NmiAttempted) ||
        !number_private("KERNEL_AP_NATIVE_INIT: ", launch.InitWrites) ||
        !number_private("KERNEL_AP_NATIVE_SIPI: ", launch.SipiWrites)) return 0;
    if (x86_percpu_ap_phase(1) == 3) {
        const XS_PRIVATE_FRAME *frame = (const XS_PRIVATE_FRAME *)(launch.Image->Record.StackTop - 1024);
        (void)number_private("KERNEL_AP_NATIVE_ENTRY_ERROR: ", frame->Error);
    }
    if (!send_nmi) {
        return !started && launch.Error == 5 && launch.Gate == 1 && !launch.NmiAttempted &&
            xs_snapshot() == (launch.Token | XS_QUARANTINED) && !x86_percpu_ap_phase(1) &&
            xp_panic_value(XP_PANIC_ONLINE) == 1 && kernel_log(0, "KERNEL_AP_NO_NMI_QUARANTINED");
    }
    int ready = started && launch.State == 4 && xs_snapshot() == (launch.Token | XS_PARKED) &&
        x86_percpu_ap_phase(1) == 2 && xp_panic_value(XP_PANIC_ONLINE) == 3 &&
        kernel_log(0, "KERNEL_AP_NATIVE_PARKED");
#ifdef KERNEL_TEST_AP_REMOTE_PANIC
    if (ready) {
        if (!kernel_log(1, "KERNEL_AP_REMOTE_PANIC_STARTING")) return 0;
        xp_panic_voluntary(UINT64_C(0x41505f50414e4943));
    }
#endif
    return ready;
}
