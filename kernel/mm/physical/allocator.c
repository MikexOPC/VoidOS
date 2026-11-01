#include "internal.h"
#include "kernel/sync/internal.h"

// ====== kernel_physical_initialize ======

int kernel_physical_initialize(KERNEL_PHYSICAL_ALLOCATOR *owner, const unsigned char *map,
    BOOT_U64 bytes, BOOT_U64 stride, BOOT_U32 bits) {
    uint64_t saved_if = ks_irq_save();
    KERNEL_LOCK_CONTEXT *context = kernel_lock_current_context();
    int result = 0;
    if (context && ks_span(owner, sizeof(*owner)) && !owner->Self && !owner->Ready &&
        !ks_overlap(owner, sizeof(*owner), context, sizeof(*context)) &&
        (!context->Wait || !ks_overlap(owner, sizeof(*owner), context->Wait, sizeof(*context->Wait))) &&
        kphys_initialize(owner, map, bytes, stride, bits)
    ) {
        if (!kernel_lock_initialize(&owner->Gate, KPHYS_LOCK_RANK))
            ks_fail_stop("PHYSICAL_LOCK_INITIALIZATION_FAILED");
        result = 1;
    }
    ks_irq_restore(saved_if);
    return result;
}

// ====== kernel_physical_validate ======

int kernel_physical_validate(const KERNEL_PHYSICAL_ALLOCATOR *owner) {
    KPHYS_ACCESS access = {0};
    if (!kphys_enter(owner, &access)) return 0;
    int result = kphys_validate(owner);
    kphys_leave(&access);
    return result;
}

// ====== kernel_physical_allocate_aligned ======

int kernel_physical_allocate_aligned(KERNEL_PHYSICAL_ALLOCATOR *owner, BOOT_U64 pages,
    BOOT_U64 alignment_pages, KERNEL_PHYSICAL_LEASE *output) {
    KPHYS_ACCESS access = {0};
    if (!kphys_enter(owner, &access)) return 0;
    int result = kphys_buffer(owner, &access, output, sizeof(*output), 1) &&
        kphys_allocate_aligned(owner, pages, alignment_pages, output);
    kphys_leave(&access);
    return result;
}

// ====== kernel_physical_allocate_within ======

int kernel_physical_allocate_within(KERNEL_PHYSICAL_ALLOCATOR *owner, BOOT_U64 pages,
    BOOT_U64 alignment_pages, BOOT_U64 minimum, BOOT_U64 limit, KERNEL_PHYSICAL_LEASE *output) {
    KPHYS_ACCESS access = {0};
    if (!kphys_enter(owner, &access)) return 0;
    int result = kphys_buffer(owner, &access, output, sizeof(*output), 1) &&
        kphys_allocate_within(owner, pages, alignment_pages, minimum, limit, output);
    kphys_leave(&access);
    return result;
}

// ====== kernel_physical_allocate ======

int kernel_physical_allocate(KERNEL_PHYSICAL_ALLOCATOR *owner, BOOT_U64 pages,
    KERNEL_PHYSICAL_LEASE *output) {
    return kernel_physical_allocate_aligned(owner, pages, 1, output);
}

// ====== kernel_physical_reserve_startup_page ======

int kernel_physical_reserve_startup_page(KERNEL_PHYSICAL_ALLOCATOR *owner, KERNEL_PHYSICAL_LEASE *output) {
    KPHYS_ACCESS access = {0};
    int result = 0;
    if (!kphys_enter(owner, &access)) return 0;
    if (kphys_buffer(owner, &access, output, sizeof(*output), 1) && kphys_validate(owner) &&
        !output->Base && !output->Pages && !output->Id && !output->Owner &&
        owner->NextId == 1 && !owner->Generation && owner->FreePages == owner->TotalPages) {
        result = 2;
#ifndef KERNEL_TEST_AP_LOW_ABSENT
        if (owner->Count && owner->Free[0].Base >= 4096 && owner->Free[0].Base <= 0xff000) {
            if (!kphys_allocate_aligned(owner, 1, 1, output) || !kphys_pin(owner, output, 1) ||
                !kphys_validate(owner)) ks_fail_stop("STARTUP_PAGE_RESERVATION_FAILED");
            result = 1;
        }
#endif
    }
    kphys_leave(&access);
    return result;
}

// ====== kernel_physical_release ======

int kernel_physical_release(KERNEL_PHYSICAL_ALLOCATOR *owner, KERNEL_PHYSICAL_LEASE *lease) {
    KPHYS_ACCESS access = {0};
    if (!kphys_enter(owner, &access)) return 0;
    int result = kphys_buffer(owner, &access, lease, sizeof(*lease), 1) && kphys_release(owner, lease);
    kphys_leave(&access);
    return result;
}

// ====== kernel_physical_pin ======

int kernel_physical_pin(KERNEL_PHYSICAL_ALLOCATOR *owner, const KERNEL_PHYSICAL_LEASE *lease, BOOT_U32 pin) {
    KPHYS_ACCESS access = {0};
    if (!kphys_enter(owner, &access)) return 0;
    int result = kphys_buffer(owner, &access, lease, sizeof(*lease), 0) && kphys_pin(owner, lease, pin);
    kphys_leave(&access);
    return result;
}

// ====== kernel_physical_is_pinned ======

int kernel_physical_is_pinned(const KERNEL_PHYSICAL_ALLOCATOR *owner, const KERNEL_PHYSICAL_LEASE *lease) {
    KPHYS_ACCESS access = {0};
    if (!kphys_enter(owner, &access)) return 0;
    int result = kphys_buffer(owner, &access, lease, sizeof(*lease), 0) && kphys_is_pinned(owner, lease);
    kphys_leave(&access);
    return result;
}

// ====== kernel_physical_release_after_unmap ======

int kernel_physical_release_after_unmap(KERNEL_PHYSICAL_ALLOCATOR *owner, KERNEL_PHYSICAL_LEASE *lease) {
    KPHYS_ACCESS access = {0};
    int result = 0;
    if (!kphys_enter(owner, &access)) return 0;
    if (kphys_buffer(owner, &access, lease, sizeof(*lease), 1) && kphys_validate(owner) &&
        kphys_is_pinned(owner, lease) && kphys_release_capacity(owner, lease)
    ) {
        if (!kphys_pin(owner, lease, 0) || !kphys_release(owner, lease))
            ks_fail_stop("PHYSICAL_RELEASE_TRANSACTION_FAILED");
        result = 1;
    }
    kphys_leave(&access);
    return result;
}
