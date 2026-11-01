#include "transition.h"
#include "kernel/sync/internal.h"
#include "../internal.h"
#include "content/ownership.h"

// ====== empty_private ======

static int empty_private(const void *pointer, size_t bytes) {
    for (size_t byte = 0; byte < bytes; ++byte) if (((const unsigned char *)pointer)[byte]) return 0;
    return 1;
}

// ====== xt_early ======

int xt_early(KERNEL_PHYSICAL_ALLOCATOR *pool, const X86_AP_LOW_MEMORY *code, X86_AP_TRANSITION *owner) {
    KERNEL_LOCK_CONTEXT *context = kernel_lock_current_context();
    KERNEL_PHYSICAL_STATUS status;
    if (!context || context->Depth || !ks_span(owner, sizeof(*owner)) || !ks_span(pool, sizeof(*pool)) ||
        !ks_span(code, sizeof(*code)) || ks_overlap(owner, sizeof(*owner), pool, sizeof(*pool)) ||
        ks_overlap(owner, sizeof(*owner), code, sizeof(*code)) ||
        ks_overlap(owner, sizeof(*owner), context, sizeof(*context)) ||
        (context->Wait && ks_overlap(owner, sizeof(*owner), context->Wait, sizeof(*context->Wait))) ||
        !empty_private(owner, sizeof(*owner)) || !x86_ap_low_valid(code) || code->Pool != pool ||
        !kernel_physical_status(pool, &status)) return 0;
    if (code->State == 2 && (status.NextId != 2 || status.LivePages != 1 || status.PinnedPages != 1)) return 0;
    owner->Self = (BOOT_U64)owner; owner->Pool = pool; owner->Code = code;
    owner->State = 2;
    if (code->State != 2) return 1;
    if (!kernel_physical_allocate_within(pool, 1, 1, 4096, 0x100000, &owner->Data)) return 1;
    if (!kernel_physical_pin(pool, &owner->Data, 1)) {
        if (!kernel_physical_release(pool, &owner->Data)) ks_fail_stop("AP_TRANSITION_DATA_ROLLBACK_FAILED");
        return 1;
    }
    if (!x86_ap_low_hold(code, owner)) {
        if (!kernel_physical_release_after_unmap(pool, &owner->Data)) ks_fail_stop("AP_TRANSITION_HOLD_ROLLBACK_FAILED");
        return 0;
    }
    owner->DataPinned = 1; owner->State = 1;
    return 1;
}

// ====== tables_release_private ======

static int tables_release_private(X86_AP_TRANSITION *owner) {
    if (owner->Tables.Id) {
        int result = owner->TablesPinned ? kernel_physical_release_after_unmap(owner->Pool, &owner->Tables) :
            kernel_physical_release(owner->Pool, &owner->Tables);
        if (!result) return 0;
    }
    owner->TablesPinned = 0;
    return 1;
}

// ====== xt_cancel ======

int xt_cancel(X86_AP_TRANSITION *owner) {
    BOOT_U64 budget;
    if (!owner || owner->Self != (BOOT_U64)owner || !owner->Pool || owner->State < 1 || owner->State > 3 ||
        owner->Reserved || xt_content_busy(owner) || x86_ap_low_is_frozen(owner->Code, owner) ||
        owner->DataPinned > 1 || owner->TablesPinned > 1 ||
        (owner->State != 2 && (!owner->Data.Id || owner->DataPinned != 1)) ||
        (owner->State == 2 && (owner->Data.Id || owner->Tables.Id)) ||
        (owner->State == 3 && (!owner->Tables.Id || owner->TablesPinned != 1)) ||
        (!owner->Data.Id && (owner->Data.Base || owner->Data.Pages || owner->Data.Owner || owner->DataPinned)) ||
        (!owner->Tables.Id && (owner->Tables.Base || owner->Tables.Pages || owner->Tables.Owner || owner->TablesPinned)) ||
        !x86_ap_low_valid(owner->Code) || owner->Code->Pool != owner->Pool ||
        (owner->Data.Id && (owner->Data.Pages != 1 || !kernel_physical_lease_state(owner->Pool, &owner->Data, owner->DataPinned))) ||
        (owner->Tables.Id && (owner->Tables.Pages != 4 || !kernel_physical_lease_state(owner->Pool, &owner->Tables, owner->TablesPinned))) ||
        (owner->Io.Safe && !owner->Io.Safe(owner->Io.Context)) ||
        !kernel_physical_budget_read(owner->Pool, &budget)) return 0;
    BOOT_U64 pages = owner->Data.Pages + owner->Tables.Pages;
    int held = owner->Data.Id != 0;
    if (held && !x86_ap_low_is_held(owner->Code, owner)) return 0;
    if (pages > 5 || budget > UINT64_MAX - pages || !tables_release_private(owner)) return 0;
    if (owner->State == 3) owner->State = 1;
    if (owner->Data.Id) {
        int result = owner->DataPinned ? kernel_physical_release_after_unmap(owner->Pool, &owner->Data) :
            kernel_physical_release(owner->Pool, &owner->Data);
        if (!result) return 0;
        owner->DataPinned = 0;
    }
    if (held && !x86_ap_low_unhold(owner->Code, owner)) return 0;
    if (!kernel_physical_budget_matches(owner->Pool, budget + pages)) return 0;
    for (size_t byte = 0; byte < sizeof(*owner); ++byte) ((volatile unsigned char *)owner)[byte] = 0;
    return 1;
}

// ====== xt_freeze ======

int xt_freeze(X86_AP_TRANSITION *owner) {
    KERNEL_LOCK_CONTEXT *context = kernel_lock_current_context();
    if (!context || context->Depth || ks_irq_state() || !ks_boot_cpu() ||
        !owner || owner->State != 3 || xt_content_busy(owner) || !xt_verify(owner) ||
        !x86_ap_low_freeze(owner->Code, owner)) return 0;
    owner->State = 4;
    return 1;
}

// ====== xt_prepare ======

int xt_prepare(X86_AP_TRANSITION *owner, BOOT_U64 root, BOOT_U64 pat, const XT_IO *io) {
    if (!owner || owner->Self != (BOOT_U64)owner || owner->State != 1 || owner->Tables.Id || xt_content_busy(owner) ||
        !x86_ap_low_valid(owner->Code) || !kernel_physical_lease_state(owner->Pool, &owner->Data, 1) ||
        !io || !io->Transfer || !io->Safe || !io->Safe(io->Context) || (pat & 255) != 6 ||
        root < 4096 || (root & 4095) || root >= (1ULL << owner->Pool->PhysicalBits) ||
        !kernel_physical_budget_read(owner->Pool, &owner->Budget)) return 0;
    owner->Root = root; owner->Pat = pat; owner->Io = *io;
    if (!kernel_physical_allocate_within(owner->Pool, 4, 1, 4096, 0x100000000ULL, &owner->Tables)) return 2;
    if (!kernel_physical_pin(owner->Pool, &owner->Tables, 1)) goto rollback;
    owner->TablesPinned = 1;
    if (!xt_geometry(owner->Code->Lease.Base, owner->Data.Base, owner->Tables.Base) ||
        kernel_regions_overlap(root, 1, owner->Tables.Base, 4) || root == owner->Data.Base ||
        root == owner->Code->Lease.Base || !xt_build(owner)) goto rollback;
    owner->State = 3;
    if (xt_verify(owner) && kernel_physical_budget_matches(owner->Pool, owner->Budget - 4)) return 1;
    owner->State = 1;
rollback:
    if (!io->Safe(io->Context) || !tables_release_private(owner) ||
        !kernel_physical_budget_matches(owner->Pool, owner->Budget)) ks_fail_stop("AP_TRANSITION_TABLE_ROLLBACK_FAILED");
    return 0;
}
