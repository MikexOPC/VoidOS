#include "native.h"
#include "../../../internal.h"
#include "../../private/frame.h"

// ====== xn_safe ======

int xn_safe(void *context) {
    const XN_LAUNCH *m = context;
    if (!m || m->Self != (uint64_t)m || !ks_boot_cpu() || ks_irq_state()) return 0;
    KERNEL_LOCK_CONTEXT *locks = kernel_lock_current_context();
    return locks && !locks->Depth && m->Group && m->Image &&
        m->Group->Header.Count >= 2 && m->Group->Header.Count <= 64 &&
        m->Image->Record.Index >= 1 && m->Image->Record.Index < m->Group->Header.Count &&
        !xp_panic_value(XP_PANIC_OWNER) && xag_group() == m->Group &&
        x86_percpu_ap_closed() == m->Group->Header.Count && xj_phase(m->Joint) == 2 &&
        xj_retained(m->Image) && xj_retained(m->Image->Transition) &&
        m->Image->State == 4 && m->Image->Transition->State == 4 &&
        m->Lapic == xp_panic_native.Lapic && xp_panic_lapic_held(m->Lapic) &&
        x86_kernel_root() == m->Group->Header.Root &&
        m->Image->Transition->Io.Safe(m->Image->Transition->Io.Context);
}

// ====== xn_prepare ======

int xn_prepare(XN_LAUNCH *m, const XJ_GROUP *joint) {
    if (!ks_span(m, sizeof(*m)) || !xp_image_contains((uintptr_t)m, sizeof(*m)) ||
        !ks_boot_cpu() || ks_irq_state() || !joint || xj_phase(joint) != 2) return 0;
    for (uint32_t byte = 0; byte < sizeof(*m); ++byte) if (((const unsigned char *)m)[byte]) return 0;
    const XAG_GROUP *group = xag_group();
    XB_INSTALLATION *image = joint->Image;
    const X86_LAPIC_MAPPING *lapic = xp_panic_native.Lapic;
    const X86_AP_RESOURCES *resources = joint->Operations.Resources;
    if (!group || group->Header.Count < 2 || x86_percpu_ap_closed() != group->Header.Count ||
        !resources || !xj_retained(resources) || !resources->Window || !image ||
        resources->Window->Pool != image->Transition->Pool ||
        image->Record.Index != 1 || image->Record.Entry != (uint64_t)xs_private_entry ||
        image->Record.Local != group->Cpus[1].Local || image->Record.ApicId != group->Cpus[1].ApicId ||
        xs_snapshot() != (image->Record.Token | XS_PREPARED) || !xb_sealed(image) ||
        !lapic || !x86_lapic_authority(lapic) || !xp_panic_lapic_held(lapic) ||
        xp_panic_value(XP_PANIC_ONLINE) != 1 || xp_panic_value(XP_PANIC_OWNER) ||
        !kernel_physical_lease_state(image->Transition->Pool, &image->Transition->Data, 1)) return 0;
    m->Self = (uint64_t)m; m->Token = image->Record.Token; m->Joint = joint;
    m->Image = image; m->Group = group; m->Lapic = lapic; m->Clock = group->Cpus[0].Expected.Clock;
    m->Window = resources->Window;
    m->Limit = kernel_clock_us(&m->Clock, 1000000);
    if (!m->Limit || m->Limit == UINT32_MAX || !xn_safe(m)) { m->State = 3; return 0; }
    m->State = 1;
    return 1;
}
