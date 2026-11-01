#include "install.h"
#include "../../internal.h"
#include "../../transition/content/ownership.h"
#include "../commit/retention.h"

// ====== zero_private ======

static int zero_private(const void *pointer, size_t bytes) {
    for (size_t byte = 0; byte < bytes; ++byte) if (((const unsigned char *)pointer)[byte]) return 0;
    return 1;
}

// ====== safe_private ======

static int safe_private(X86_AP_TRANSITION *t) {
    KERNEL_LOCK_CONTEXT *locks = kernel_lock_current_context();
    return ks_span(t, sizeof(*t)) && t->Self == (uint64_t)t && (t->State == 3 || t->State == 4) &&
        ks_span(t->Pool, sizeof(*t->Pool)) && ks_span(t->Code, sizeof(*t->Code)) &&
        x86_ap_low_valid(t->Code) && t->Code->Pool == t->Pool && x86_ap_low_is_held(t->Code, t) &&
        locks && !locks->Depth && !ks_irq_state() && ks_boot_cpu() &&
        x86_ap_low_is_frozen(t->Code, t) == (t->State == 4) &&
        t->Io.Safe && t->Io.Transfer && t->Io.Safe(t->Io.Context);
}

// ====== binding_private ======

static int binding_private(X86_AP_TRANSITION *t, XS_ENTRY_RECORD r) {
    return safe_private(t) && r.Code == t->Code->Lease.Base && r.Data == t->Data.Base &&
        r.TransitionRoot == t->Tables.Base && r.KernelRoot == t->Root && r.Pat == t->Pat &&
        xs_entry_valid(r, t->Pool->PhysicalBits);
}

// ====== owner_private ======

static int owner_private(XB_INSTALLATION *owner) {
    return ks_span(owner, sizeof(*owner)) && owner->Self == (uint64_t)owner && !owner->Reserved &&
        owner->State >= 1 && owner->State <= 4 && ks_span(owner->Template, XB_PAGE) &&
        xt_content_held(owner->Transition, owner) &&
        binding_private(owner->Transition, owner->Record);
}

// ====== read_private ======

static int read_private(XB_INSTALLATION *owner, uint64_t physical, const uint64_t *expected) {
    X86_AP_TRANSITION *t = owner->Transition;
    if (!safe_private(t) || !t->Io.Transfer(t->Io.Context, physical, owner->Scratch, 0)) return 0;
    for (uint32_t i = 0; i < 512; ++i) if (owner->Scratch[i] != expected[i]) return 0;
    return safe_private(t);
}

// ====== xb_installed ======

int xb_installed(XB_INSTALLATION *owner) {
    if (!owner_private(owner) || owner->State != 2 || owner->Transition->State != 3) return 0;
    X86_AP_TRANSITION *t = owner->Transition;
    return xb_verify((const unsigned char *)owner->Code, XB_PAGE, (const unsigned char *)owner->Data,
        XB_PAGE, owner->Template, XB_PAGE, owner->Record, t->Pool->PhysicalBits) && xt_verify_tables(t) &&
        read_private(owner, owner->Record.Code, owner->Code) &&
        read_private(owner, owner->Record.Data, owner->Data) && xt_verify_tables(t);
}

// ====== xb_install ======

int xb_install(XB_INSTALLATION *owner, X86_AP_TRANSITION *t, const unsigned char *image, XS_ENTRY_RECORD record) {
    KERNEL_LOCK_CONTEXT *locks = kernel_lock_current_context();
    if (!ks_span(owner, sizeof(*owner)) || !ks_span(t, sizeof(*t)) || !ks_span(image, XB_PAGE) ||
        !locks || ks_overlap(owner, sizeof(*owner), t, sizeof(*t)) ||
        ks_overlap(owner, sizeof(*owner), image, XB_PAGE) ||
        ks_overlap(image, XB_PAGE, t, sizeof(*t)) || !binding_private(t, record) || t->State != 3 || xj_retained(t) ||
        ks_overlap(owner, sizeof(*owner), t->Pool, sizeof(*t->Pool)) ||
        ks_overlap(owner, sizeof(*owner), t->Code, sizeof(*t->Code)) ||
        ks_overlap(owner, sizeof(*owner), locks, sizeof(*locks)) ||
        (locks->Wait && ks_overlap(owner, sizeof(*owner), locks->Wait, sizeof(*locks->Wait))) ||
        !zero_private(owner, sizeof(*owner)) || !xt_verify(t)) return 0;
    if (!xb_build((unsigned char *)owner->Code, XB_PAGE, (unsigned char *)owner->Data,
        XB_PAGE, image, XB_PAGE, record, t->Pool->PhysicalBits)) return 0;
    if (!xt_content_hold(t, owner)) {
        for (size_t byte = 0; byte < sizeof(*owner); ++byte) ((volatile unsigned char *)owner)[byte] = 0;
        return 0;
    }
    owner->Self = (uint64_t)owner; owner->Transition = t; owner->Template = image;
    owner->Record = record; owner->State = 1;
    if (!safe_private(t) || !t->Io.Transfer(t->Io.Context, record.Code, owner->Code, 1) ||
        !safe_private(t) || !t->Io.Transfer(t->Io.Context, record.Data, owner->Data, 1)) goto failed;
    owner->State = 2;
    if (xb_installed(owner)) return 1;
failed:
    owner->State = 3;
    return 0;
}

// ====== xb_erase ======

int xb_erase(XB_INSTALLATION *owner) {
    if (!owner_private(owner) || owner->State > 3 || owner->Transition->State != 3 ||
        xj_retained(owner) || xj_retained(owner->Transition) || !xt_verify_tables(owner->Transition)) return 0;
    X86_AP_TRANSITION *t = owner->Transition;
    owner->State = 3;
    for (uint32_t i = 0; i < 512; ++i) owner->Scratch[i] = 0;
    if (!safe_private(t) || !t->Io.Transfer(t->Io.Context, owner->Record.Code, owner->Scratch, 1) ||
        !safe_private(t) || !t->Io.Transfer(t->Io.Context, owner->Record.Data, owner->Scratch, 1)) return 0;
    if (!safe_private(t) || !t->Io.Transfer(t->Io.Context, owner->Record.Code, owner->Scratch, 0) ||
        !zero_private(owner->Scratch, XB_PAGE) || !safe_private(t) ||
        !t->Io.Transfer(t->Io.Context, owner->Record.Data, owner->Scratch, 0) ||
        !zero_private(owner->Scratch, XB_PAGE) || !xt_verify(t) || !xt_content_unhold(t, owner)) return 0;
    for (size_t byte = 0; byte < sizeof(*owner); ++byte) ((volatile unsigned char *)owner)[byte] = 0;
    return 1;
}

// ====== xb_sealed ======

int xb_sealed(XB_INSTALLATION *owner) {
    if (!owner_private(owner) || owner->State != 4 || owner->Transition->State != 4 || !xj_retained(owner)) return 0;
    X86_AP_TRANSITION *t = owner->Transition;
    return xb_verify((const unsigned char *)owner->Code, XB_PAGE, (const unsigned char *)owner->Data,
        XB_PAGE, owner->Template, XB_PAGE, owner->Record, t->Pool->PhysicalBits) && xt_verify_tables(t) &&
        read_private(owner, owner->Record.Code, owner->Code) &&
        read_private(owner, owner->Record.Data, owner->Data) && xt_verify_tables(t);
}

// ====== xb_seal ======

int xb_seal(XB_INSTALLATION *owner) {
    if (!owner_private(owner) || !xj_retained(owner) || !xj_retained(owner->Transition) ||
        !xb_installed(owner) || !x86_ap_low_freeze(owner->Transition->Code, owner->Transition)) return 0;
    owner->Transition->State = 4; owner->State = 4;
    return xb_sealed(owner);
}
