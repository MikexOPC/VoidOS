#include "probe.h"
#include "arch/x86/kernel/cpu/percpu/resources/resources.h"
#include "arch/x86/kernel/cpu/percpu/clock/clock.h"
#include "arch/x86/kernel/cpu/percpu/transition/content/ownership.h"
#include "arch/x86/kernel/cpu/percpu/internal.h"
#include "kernel/diagnostics/log.h"
#ifdef KERNEL_TEST_AP_LAUNCH
#include "launch.h"
#endif
#if KERNEL_TEST_AP_TRANSITION == 9
#include "arch/x86/kernel/cpu/percpu/panic/native/runtime.h"
#include "arch/x86/kernel/cpu/percpu/admission/native.h"
#include "arch/x86/kernel/cpu/percpu/admission/entry/entry.h"
#endif
static XJ_GROUP group;
static XJ_OPERATIONS native_operations;
static XB_INSTALLATION *installed;
_Static_assert(sizeof(X86_AP_RESOURCES) == 192 && offsetof(X86_AP_RESOURCES, Self) == 104 &&
    offsetof(X86_AP_RESOURCES, Published) == 176 && offsetof(X86_AP_RESOURCES, Committed) == 184,
    "Joint resource monitor ABI");

// ====== seal_private ======

static int seal_private(void *context, uint32_t cpus) {
    X86_AP_RESOURCES *resources = context;
#if KERNEL_TEST_AP_TRANSITION != 7
    if (!native_operations.Seal(context, cpus)) return 0;
#else
    (void)cpus;
#endif
    BOOT_U32 saved = resources->Committed;
    resources->Committed = 0;
    if (x86_ap_unbind(resources)) ks_fail_stop("AP_JOINT_GUARD_BROKEN");
    resources->Committed = saved;
    X86_AP_TRANSITION *t = installed->Transition;
    if (xb_erase(installed) || xt_cancel(t) || xt_content_unhold(t, installed) ||
        x86_percpu_wait_ap_cancel(1) || !xj_context_retained(1) ||
        t->Io.Transfer(t->Io.Context, installed->Record.Code, installed->Scratch, 1) ||
        !kernel_log(0, "KERNEL_AP_JOINT_RETENTION_GUARDS_OK")) ks_fail_stop("AP_JOINT_GUARD_BROKEN");
    return KERNEL_TEST_AP_TRANSITION == 6 || KERNEL_TEST_AP_TRANSITION == 9;
}

#if KERNEL_TEST_AP_TRANSITION == 9
static uint32_t post_close_capture;

// ====== unexpected_capture_private ======

static int unexpected_capture_private(const KERNEL_BOOT_CONTEXT *context, const KERNEL_PHYSICAL_ALLOCATOR *pool,
    const X86_LAPIC_MAPPING *lapic, X86_PAGING_INVENTORY *inventory) {
    (void)context; (void)pool; (void)lapic; (void)inventory;
    ++post_close_capture;
    return 0;
}

// ====== admission_rejections_private ======

static int admission_rejections_private(X86_AP_RESOURCES *resources) {
    if (xag_native_publish(NULL) || xag_group()) return 0;
    uint32_t committed = resources->Committed;
    resources->Committed = 0;
    int refused = !xag_native_publish(&group) && !xag_group();
    resources->Committed = committed;
    if (!refused) return 0;
    uint64_t root = resources->Authority.Root;
    resources->Authority.Root ^= 4096;
    refused = !xag_native_publish(&group) && !xag_group();
    resources->Authority.Root = root;
    if (!refused) return 0;
    X86_CPU_LOCAL *owner = (X86_CPU_LOCAL *)x86_percpu_ap(1);
    if (!owner || !owner->Locks.Wait) return 0;
    const KERNEL_LOCK_WAIT *wait = owner->Locks.Wait;
    owner->Locks.Wait = NULL;
    refused = !xag_native_publish(&group) && !xag_group();
    owner->Locks.Wait = wait;
    if (!refused) return 0;
    X86_PM_CLOCK *source = wait->Clock.Context;
    uint32_t ready = source->Ready;
    source->Ready = 0;
    refused = !xag_native_publish(&group) && !xag_group();
    source->Ready = ready;
    return refused && kernel_log(0, "KERNEL_AP_ADMISSION_REJECTIONS_OK");
}
#endif

// ====== xj_probe ======

int xj_probe(XB_INSTALLATION *image) {
    if (!xj_native_operations(&native_operations)) return 0;
    installed = image;
    XJ_OPERATIONS operations = native_operations;
    operations.Seal = seal_private;
    if (!kernel_text("KERNEL_AP_JOINT_OWNER: ") || !kernel_number((uint64_t)&group) ||
        !kernel_text("\nKERNEL_AP_JOINT_RESOURCES: ") || !kernel_number((uint64_t)operations.Resources) ||
        !kernel_text("\nKERNEL_AP_JOINT_CPUS: ") || !kernel_number(operations.Cpus) ||
        !kernel_text("\n") || !kernel_log(1, "KERNEL_AP_JOINT_ARMED")) return 0;
    if (!xj_commit(&group, image, &operations) || xj_phase(&group) != 2 || !xb_sealed(image) ||
        xj_commit(&group, image, &operations) || !x86_ap_resources_committed_valid(operations.Resources)) return 0;
    if (!kernel_log(0, "KERNEL_AP_JOINT_SEALED")) return 0;
#if KERNEL_TEST_AP_TRANSITION == 9
    X86_AP_RESOURCES *resources = (X86_AP_RESOURCES *)operations.Resources;
    if (xag_group() || xag_native_publish(&group) || !x86_ap_resources_joint_ready() || !xp_panic_native_prepare(resources->Lapic) ||
        xp_panic_native.Count != operations.Cpus || !ks_atomic_load_acquire(&xp_panic_armed) ||
        !xb_sealed(image) || xj_phase(&group) != 2) return 0;
    if (!kernel_text("KERNEL_AP_PANIC_IDTR: ") || !kernel_number((uint64_t)&xp_panic_native.Idtr) ||
        !kernel_text("\nKERNEL_AP_PANIC_RECORD: ") || !kernel_number((uint64_t)xp_panic_native.Records[1]) ||
        !kernel_text("\n") || !kernel_log(0, "KERNEL_AP_JOINT_PANIC_PREPARED")) return 0;
    if (!admission_rejections_private(resources) || !xag_native_publish(&group) || !xag_group() || xag_native_publish(&group) ||
        xp_panic_value(XP_PANIC_ONLINE) != 1) return 0;
    const XAG_GROUP *admission = xag_group();
    for (uint32_t index = 0; index < admission->Header.Count; ++index)
        if (xag_match(index, xp_panic_native.Ids[index], (uint64_t)xp_panic_native.Contexts[index]) != &admission->Cpus[index] ||
            xag_match(index, 255, admission->Cpus[index].Local) || xag_match(index, admission->Cpus[index].ApicId, 0)) return 0;
    if (!kernel_text("KERNEL_AP_ADMISSION_GROUP: ") || !kernel_number((uint64_t)admission) ||
        !kernel_text("\n") || !kernel_log(0, "KERNEL_AP_ADMISSION_GROUP_PUBLISHED")) return 0;
    if (!xa_native_admit(NULL, 0) || !xa_native_park(NULL, 0) || !xag_native_close() || xag_native_close() ||
        x86_percpu_ap_closed() != admission->Header.Count || x86_percpu_ap(1) || x86_percpu_wait_ap_valid(1) ||
        x86_percpu_ap_accept(1) || x86_percpu_ap_phase(1) || xp_panic_value(XP_PANIC_ONLINE) != 1 ||
        !kernel_log(0, "KERNEL_AP_CONTEXT_ACCESS_CLOSED")) return 0;
    X86_AP_CAPTURE saved_capture = resources->Capture;
    resources->Capture = unexpected_capture_private;
    int blocked = !x86_ap_resources_committed_valid(resources) && !x86_ap_resources_joint_ready() &&
        !x86_ap_resources_preflight() && !post_close_capture;
    resources->Capture = saved_capture;
    if (!blocked || !kernel_log(0, "KERNEL_AP_MUTABLE_PROBES_BLOCKED")) return 0;
#ifdef KERNEL_TEST_AP_LAUNCH
    if (!xn_probe(&group, KERNEL_TEST_AP_LAUNCH == 1)) return 0;
#endif
#endif
    return 1;
}
