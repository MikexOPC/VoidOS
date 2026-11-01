#include "native.h"
#include "../panic/native/runtime.h"
static XAG_GROUP draft;
static uint32_t collecting;

// ====== native_private ======

static int native_private(const XJ_GROUP *group) {
    const X86_CPU_LOCAL *bsp = x86_percpu_bsp();
    const KERNEL_LOCK_WAIT *bsp_wait = x86_percpu_wait_current();
    const X86_CPU_IDENTITIES *ids = x86_cpu_identities();
    const XP_PANIC_NATIVE *panic = &xp_panic_native;
    if (!bsp || !bsp_wait || bsp->Locks.Wait != bsp_wait || bsp->Locks.Depth || !ids || !group ||
        !xp_image_contains((uintptr_t)group, sizeof(*group)) ||
        xj_phase(group) != 2 || !xb_sealed(group->Image) ||
        !ks_atomic_load_acquire(&xp_panic_armed) || xp_panic_value(XP_PANIC_READY) != 2 ||
        xp_panic_value(XP_PANIC_ONLINE) != 1 || xp_panic_value(XP_PANIC_OWNER) ||
        ids->Count < 2 || ids->Count != panic->Count || !x86_percpu_ap_sealed(ids->Count)) return 0;
    const X86_AP_RESOURCES *resources = group->Operations.Resources;
    if (!resources || !xp_image_contains((uintptr_t)resources, sizeof(*resources)) ||
        !xj_retained(resources) || resources->Self != (uint64_t)resources || resources->Committed != 1 ||
        x86_ap_owner() != &resources->Authority || group->Operations.Cpus != ids->Count ||
        resources->Authority.Cpus != ids->Count || resources->Authority.Root != panic->Root ||
        resources->Authority.Pat != panic->Pat || resources->Lapic != panic->Lapic) return 0;
    draft.Header = (XAG_HEADER){1, sizeof(draft), ids->Count, resources->Authority.PhysicalBits, 0, panic->Root, panic->Pat};
    for (uint32_t index = 0; index < ids->Count; ++index) {
        const X86_CPU_LOCAL *owner = index ? x86_percpu_ap(index) : bsp;
        if (!owner || panic->Contexts[index] != owner || panic->Ids[index] != ids->Cpus[index].Id ||
            (index && !x86_percpu_wait_ap_valid(index))) return 0;
        const KERNEL_LOCK_WAIT *wait = owner->Locks.Wait;
        if (!wait || !xp_image_contains((uintptr_t)wait, sizeof(*wait)) ||
            !xp_image_contains((uintptr_t)wait->Clock.Context, sizeof(X86_PM_CLOCK)) ||
            !xp_image_contains((uintptr_t)wait->Clock.Read, 1) ||
            !xp_image_contains((uintptr_t)owner, sizeof(*owner))) return 0;
        draft.Cpus[index] = (XAG_CPU){index, ids->Cpus[index].Id, (uint64_t)owner, (uint64_t)wait,
            *wait, *(const X86_PM_CLOCK *)wait->Clock.Context};
    }
    return xag_publish(&draft);
}

// ====== xag_native_publish ======

int xag_native_publish(const XJ_GROUP *group) {
    if (ks_irq_state() || !ks_boot_cpu() || collecting || xag_group()) return 0;
    collecting = 1;
    int result = native_private(group);
    for (uint32_t byte = 0; byte < sizeof(draft); ++byte) ((volatile unsigned char *)&draft)[byte] = 0;
    collecting = 0;
    return result;
}

// ====== xag_native_close ======

int xag_native_close(void) {
    if (ks_irq_state() || !ks_boot_cpu()) return 0;
    const XAG_GROUP *group = xag_group();
    if (!group || group->Header.Count < 2 || group->Header.Count > 64 ||
        !ks_atomic_load_acquire(&xp_panic_armed) || xp_panic_value(XP_PANIC_ONLINE) != 1 ||
        xp_panic_value(XP_PANIC_OWNER)) return 0;
    uint32_t ids[64];
    for (uint32_t index = 0; index < group->Header.Count; ++index) ids[index] = group->Cpus[index].ApicId;
    return x86_percpu_ap_access_close(group->Header.Count, ids);
}
