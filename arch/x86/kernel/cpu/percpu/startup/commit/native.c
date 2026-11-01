#include "commit.h"
#include "../../resources/resources.h"
#include "../../identity/identity.h"
#include "../../internal.h"

// ====== check_private ======

static int check_private(void *context, XB_INSTALLATION *image, uint32_t cpus) {
    const X86_AP_RESOURCES *resources = context;
    const X86_CPU_IDENTITIES *ids = x86_cpu_identities();
    if (resources != x86_ap_resources_preflight() || !ids || cpus != ids->Count || !image ||
        image->Record.Index >= cpus || !xp_image_contains((uintptr_t)image->Template, XB_PAGE)) return 0;
    const X86_CPU_LOCAL *local = x86_percpu_ap(image->Record.Index);
    const X86_AP_TRANSITION *t = image->Transition;
    return local && image->Record.ApicId == ids->Cpus[image->Record.Index].Id && image->Record.Local == (uint64_t)local &&
        xp_image_contains((uintptr_t)image->Record.Entry, 1) && resources->Window && resources->Window->Pool == t->Pool &&
        resources->Authority.Root == t->Root && resources->Authority.Pat == t->Pat &&
        resources->Authority.PhysicalBits == t->Pool->PhysicalBits && resources->Authority.Cpus == cpus;
}

// ====== seal_private ======

static int seal_private(void *context, uint32_t cpus) {
    const X86_AP_RESOURCES *resources = context;
    return resources->Authority.Cpus == cpus && x86_ap_resources_joint_seal(resources);
}

// ====== verify_private ======

static int verify_private(void *context, uint32_t cpus) {
    const X86_AP_RESOURCES *resources = context;
    return resources->Authority.Cpus == cpus && x86_ap_resources_committed_valid(resources);
}

// ====== xj_native_operations ======

int xj_native_operations(XJ_OPERATIONS *output) {
    const X86_AP_RESOURCES *resources = x86_ap_resources_preflight();
    if (!resources || !ks_span(output, sizeof(*output)) ||
        ks_overlap(output, sizeof(*output), resources, sizeof(*resources))) return 0;
    *output = (XJ_OPERATIONS){(void *)resources, resources, resources->Authority.Cpus,
        sizeof(*resources), check_private, seal_private, verify_private};
    return 1;
}
