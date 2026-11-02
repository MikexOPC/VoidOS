#include "runtime.h"
#include "../transport/native/native.h"
#include "../private/frame.h"
#include "../../admission/native.h"
#include "../../identity/identity.h"
#include "../../internal.h"
#include "kernel/diagnostics/log.h"

static XB_INSTALLATION image;
static XJ_GROUP group;
static XN_LAUNCH launch;
static uint32_t attempted;
extern const unsigned char xs_blob_template[4096];

// ====== number_private ======

static int number_private(const char *name, uint64_t value) {
    return kernel_text(name) && kernel_number(value) && kernel_text("\n");
}

// ====== reject_private ======

static int reject_private(X86_AP_TRANSITION *t, const char *reason) {
    if (!xj_retained(t)) {
        if ((image.Self && !xb_erase(&image)) || !xt_cancel(t)) ks_fail_stop("AP_RUNTIME_ROLLBACK_FAILED");
    }
    (void)number_private("KERNEL_AP_RUNTIME_ERROR: ", launch.Error);
    (void)kernel_log(3, reason);
    return 0;
}

// ====== x86_ap_runtime_start ======

int x86_ap_runtime_start(X86_AP_TRANSITION *t, const X86_LAPIC_MAPPING *lapic) {
    if (ks_irq_state() || !ks_boot_cpu()) return 0;
    const X86_CPU_IDENTITIES *ids = x86_cpu_identities();
    KERNEL_LOCK_CONTEXT *locks = kernel_lock_current_context();
    if (attempted || !t || !ids || !ids->Count || ids->Count > 64 || !locks || locks->Depth ||
        !lapic) return 0;
    attempted = 1;
    if (ids->Count == 1) return xp_panic_native_prepare(lapic) && kernel_log(0, "KERNEL_AP_RUNTIME_BSP_ONLY");
    if (t->Self != (uint64_t)t || t->State != 3 || !t->Pool || !t->Code) return 0;
    const X86_CPU_LOCAL *local = x86_percpu_ap(1);
    XS_ENTRY_RECORD record;
    if (!local || !xs_configure(ids->Count)) return reject_private(t, "AP_RUNTIME_CONFIGURATION_FAILED");
    uint64_t token = xs_reserve(1);
    XS_ENTRY_INPUT input = {1, ids->Cpus[1].Id, t->Pool->PhysicalBits, 0, token,
        t->Code->Lease.Base, t->Data.Base, t->Tables.Base, t->Root, (uint64_t)local,
        (uint64_t)xs_private_entry, t->Pat};
    if (!token || !xs_entry_build(&record, input) || !xb_install(&image, t, xs_blob_template, record))
        return reject_private(t, "AP_RUNTIME_INSTALL_FAILED");
    if (!number_private("KERNEL_AP_IMAGE_OWNER: ", (uint64_t)&image) ||
        !number_private("KERNEL_AP_IMAGE_CODE_EXPECTED: ", (uint64_t)image.Code) ||
        !number_private("KERNEL_AP_IMAGE_DATA_EXPECTED: ", (uint64_t)image.Data))
        return reject_private(t, "AP_RUNTIME_LOG_FAILED");
    XJ_OPERATIONS operations;
    if (!xj_native_operations(&operations) || !xj_commit(&group, &image, &operations))
        return reject_private(t, "AP_RUNTIME_COMMIT_FAILED");
    if (!xp_panic_native_prepare(lapic) || !xag_native_publish(&group) || !xag_native_close() ||
        !number_private("KERNEL_AP_PANIC_IDTR: ", (uint64_t)&xp_panic_native.Idtr) ||
        !xn_prepare(&launch, &group)) return reject_private(t, "AP_RUNTIME_ADMISSION_FAILED");
    for (uint32_t index = 1; index < ids->Count; ++index) {
        if ((index > 1 && !xn_next(&launch)) || !xn_run(&launch, 1) || !xn_private_parked(&launch))
            return reject_private(t, "AP_RUNTIME_START_FAILED");
    }
    uint64_t expected = UINT64_MAX >> (64 - ids->Count);
    if (!xt_verify_live_tables(t) || launch.State != 4 || xs_snapshot() != (launch.Token | XS_PARKED) ||
        xp_panic_value(XP_PANIC_ONLINE) != expected || xp_panic_value(XP_PANIC_OWNER) ||
        !number_private("KERNEL_AP_RUNTIME_DISCOVERED: ", ids->Count) ||
        !number_private("KERNEL_AP_RUNTIME_PARKED: ", ids->Count - 1) ||
        !number_private("KERNEL_AP_RUNTIME_ONLINE: ", expected) ||
        !kernel_log(0, "KERNEL_AP_RUNTIME_READY")) return reject_private(t, "AP_RUNTIME_FINAL_FAILED");
    return 1;
}
