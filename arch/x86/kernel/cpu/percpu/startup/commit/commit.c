#include "commit.h"
#include "internal/registry.h"
#include "../../internal.h"
static uint32_t preparing;

// ====== empty_private ======

static int empty_private(const XJ_GROUP *group) {
    for (size_t byte = 0; byte < sizeof(*group); ++byte) if (((const unsigned char *)group)[byte]) return 0;
    return 1;
}

// ====== valid_private ======

static int valid_private(XJ_GROUP *group, XB_INSTALLATION *image, const XJ_OPERATIONS *ops) {
    const KERNEL_LOCK_CONTEXT *locks = kernel_lock_current_context();
    return locks && !locks->Depth && !ks_irq_state() && ks_boot_cpu() &&
        ks_span(group, sizeof(*group)) && ks_span(image, sizeof(*image)) && ks_span(ops, sizeof(*ops)) &&
        ks_span(image->Transition, sizeof(*image->Transition)) && ops->Bytes && ops->Bytes <= 4096 &&
        ks_span(image->Transition->Pool, sizeof(*image->Transition->Pool)) &&
        ks_span(image->Transition->Code, sizeof(*image->Transition->Code)) &&
        ops->Resources == ops->Context && ks_span(ops->Resources, ops->Bytes) &&
        ks_span(ops->Context, ops->Bytes) && ops->Cpus >= 2 && ops->Cpus <= 64 &&
        image->Record.Index < ops->Cpus && ops->Check && ops->Seal && ops->Verify &&
        !ks_overlap(group, sizeof(*group), image, sizeof(*image)) &&
        !ks_overlap(group, sizeof(*group), image->Transition, sizeof(*image->Transition)) &&
        !ks_overlap(group, sizeof(*group), image->Template, XB_PAGE) &&
        !ks_overlap(group, sizeof(*group), ops, sizeof(*ops)) &&
        !ks_overlap(group, sizeof(*group), ops->Resources, ops->Bytes) &&
        !ks_overlap(group, sizeof(*group), ops->Context, ops->Bytes) &&
        !ks_overlap(group, sizeof(*group), image->Transition->Pool, sizeof(*image->Transition->Pool)) &&
        !ks_overlap(group, sizeof(*group), image->Transition->Code, sizeof(*image->Transition->Code)) &&
        (!locks->Wait || !ks_overlap(group, sizeof(*group), locks->Wait, sizeof(*locks->Wait))) &&
        !ks_overlap(group, sizeof(*group), locks, sizeof(*locks)) && empty_private(group);
}

// ====== failed_private ======

__declspec(noreturn) static void failed_private(XJ_GROUP *group, uint32_t reason) {
    group->State = 3; group->Failure = reason;
    (void)xj_finish(group, 3);
    ks_fail_stop(reason == 1 ? "AP_JOINT_IMAGE_SEAL_FAILED" :
        reason == 2 ? "AP_JOINT_PRIVATE_SEAL_FAILED" : "AP_JOINT_FINAL_VERIFY_FAILED");
}

// ====== xj_commit ======

int xj_commit(XJ_GROUP *group, XB_INSTALLATION *image, const XJ_OPERATIONS *operations) {
    if (preparing || xj_active() || !valid_private(group, image, operations)) return 0;
    XJ_OPERATIONS ops = *operations;
    preparing = 1;
    if (!xb_installed(image) || !ops.Check(ops.Context, image, ops.Cpus) ||
        !valid_private(group, image, &ops)) { preparing = 0; return 0; }
    group->Self = (uint64_t)group; group->Image = image; group->Operations = ops;
    if (!xj_latch(group, image, image->Transition, ops.Resources, ops.Cpus)) {
        for (size_t byte = 0; byte < sizeof(*group); ++byte) ((volatile unsigned char *)group)[byte] = 0;
        preparing = 0;
        return 0;
    }
    group->State = 1;
    if (!xb_seal(image)) failed_private(group, 1);
    if (!ops.Seal(ops.Context, ops.Cpus)) failed_private(group, 2);
    if (!xb_sealed(image) || !ops.Verify(ops.Context, ops.Cpus)) failed_private(group, 3);
    if (!xj_finish(group, 2)) failed_private(group, 3);
    group->State = 2;
    preparing = 0;
    return 1;
}
