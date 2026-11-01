#include "ownership.h"
#include "../../internal.h"
#include "../../startup/commit/retention.h"
static const X86_AP_TRANSITION *held_owner;
static const void *held_borrower;

// ====== xt_content_busy ======

int xt_content_busy(const X86_AP_TRANSITION *transition) { return held_owner && held_owner == transition; }

// ====== xt_content_held ======

int xt_content_held(const X86_AP_TRANSITION *transition, const void *borrower) {
    return xt_content_busy(transition) && held_borrower == borrower;
}

// ====== safe_private ======

static int safe_private(X86_AP_TRANSITION *t, const void *borrower) {
    KERNEL_LOCK_CONTEXT *locks = kernel_lock_current_context();
    return ks_span(t, sizeof(*t)) && ks_span(borrower, 8) &&
        !ks_overlap(t, sizeof(*t), borrower, 8) && locks && !locks->Depth &&
        !ks_irq_state() && ks_boot_cpu() && t->State == 3 && !x86_ap_low_is_frozen(t->Code, t) && xt_verify(t);
}

// ====== xt_content_hold ======

int xt_content_hold(X86_AP_TRANSITION *t, const void *borrower) {
    if (held_owner || !safe_private(t, borrower)) return 0;
    held_owner = t; held_borrower = borrower;
    return 1;
}

// ====== xt_content_unhold ======

int xt_content_unhold(X86_AP_TRANSITION *t, const void *borrower) {
    if (xj_retained(t) || !xt_content_held(t, borrower) || !safe_private(t, borrower)) return 0;
    if (!t->Io.Transfer(t->Io.Context, t->Code->Lease.Base, t->Scratch, 0)) return 0;
    for (uint32_t i = 0; i < 512; ++i) if (t->Scratch[i]) return 0;
    if (!t->Io.Safe(t->Io.Context)) return 0;
    held_owner = 0; held_borrower = 0;
    return 1;
}
