#include "lowmemory.h"
#include "kernel/sync/internal.h"
#include "../internal.h"
static const X86_AP_LOW_MEMORY *held_owner;
static const void *held_borrower;
static BOOT_U32 held_frozen;

// ====== x86_ap_low_hold ======

int x86_ap_low_hold(const X86_AP_LOW_MEMORY *owner, const void *borrower) {
    if (held_owner || !ks_span(borrower, 8) || !x86_ap_low_valid(owner) || owner->State != 2) return 0;
    held_owner = owner; held_borrower = borrower;
    return 1;
}

// ====== x86_ap_low_unhold ======

int x86_ap_low_unhold(const X86_AP_LOW_MEMORY *owner, const void *borrower) {
    if (held_frozen || !held_owner || held_owner != owner || held_borrower != borrower || !x86_ap_low_valid(owner)) return 0;
    held_owner = 0; held_borrower = 0;
    return 1;
}

// ====== x86_ap_low_is_held ======

int x86_ap_low_is_held(const X86_AP_LOW_MEMORY *owner, const void *borrower) {
    return held_owner && held_owner == owner && held_borrower == borrower;
}

// ====== x86_ap_low_is_frozen ======

int x86_ap_low_is_frozen(const X86_AP_LOW_MEMORY *owner, const void *borrower) {
    return held_frozen && x86_ap_low_is_held(owner, borrower);
}

// ====== x86_ap_low_freeze ======

int x86_ap_low_freeze(const X86_AP_LOW_MEMORY *owner, const void *borrower) {
    KERNEL_LOCK_CONTEXT *context = kernel_lock_current_context();
    if (held_frozen || !context || context->Depth || ks_irq_state() || !ks_boot_cpu() ||
        !x86_ap_low_is_held(owner, borrower) || !x86_ap_low_valid(owner) || owner->State != 2) return 0;
    held_frozen = 1;
    return 1;
}

// ====== empty_private ======

static int empty_private(const X86_AP_LOW_MEMORY *owner) {
    for (BOOT_U32 byte = 0; byte < sizeof(*owner); ++byte)
        if (((const unsigned char *)owner)[byte]) return 0;
    return 1;
}

// ====== x86_ap_low_valid ======

int x86_ap_low_valid(const X86_AP_LOW_MEMORY *owner) {
    if (!ks_span(owner, sizeof(*owner)) || owner->Self != (BOOT_U64)owner || !owner->Pool ||
        owner->Reserved || ks_overlap(owner, sizeof(*owner), owner->Pool, sizeof(*owner->Pool))) return 0;
    if (owner->State == 1) return !owner->Lease.Base && !owner->Lease.Pages &&
        !owner->Lease.Id && !owner->Lease.Owner && kernel_physical_validate(owner->Pool);
    return owner->State == 2 && x86_ap_low_geometry(owner->Lease.Base, owner->Lease.Pages) &&
        kernel_physical_lease_state(owner->Pool, &owner->Lease, 1);
}

// ====== x86_ap_low_reserve ======

int x86_ap_low_reserve(KERNEL_PHYSICAL_ALLOCATOR *pool, X86_AP_LOW_MEMORY *owner) {
    KERNEL_LOCK_CONTEXT *context = kernel_lock_current_context();
    if (!ks_span(owner, sizeof(*owner)) || !ks_span(pool, sizeof(*pool)) ||
        !context || context->Depth || ks_overlap(owner, sizeof(*owner), context, sizeof(*context)) ||
        (context->Wait && ks_overlap(owner, sizeof(*owner), context->Wait, sizeof(*context->Wait))) ||
        ks_overlap(owner, sizeof(*owner), pool, sizeof(*pool)) || !empty_private(owner)) return 0;
    int result = kernel_physical_reserve_startup_page(pool, &owner->Lease);
    if (!result) return 0;
    owner->Self = (BOOT_U64)owner; owner->Pool = pool;
    owner->State = result == 1 ? 2U : 1U;
    if (!x86_ap_low_valid(owner)) ks_fail_stop("AP_LOW_RESERVATION_INVALID");
    return 1;
}

// ====== x86_ap_low_release ======

int x86_ap_low_release(X86_AP_LOW_MEMORY *owner) {
    if (held_owner == owner || !x86_ap_low_valid(owner)) return 0;
    if (owner->State == 2 && !kernel_physical_release_after_unmap(owner->Pool, &owner->Lease)) return 0;
    *owner = (X86_AP_LOW_MEMORY){0};
    return 1;
}
