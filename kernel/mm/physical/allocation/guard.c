#include "kernel/mm/physical/internal.h"
#include "kernel/sync/internal.h"

// ====== kphys_enter ======

int kphys_enter(const KERNEL_PHYSICAL_ALLOCATOR *owner, KPHYS_ACCESS *access) {
    access->SavedIf = ks_irq_save();
    access->Context = kernel_lock_current_context();
    if (!access->Context || !ks_span(owner, sizeof(*owner)) ||
        owner->Self != (uintptr_t)owner || owner->Ready != 1 ||
        owner->Gate.Rank != KPHYS_LOCK_RANK ||
        !kernel_lock_acquire((KERNEL_LOCK *)&owner->Gate, access->Context, &access->Guard)
    ) {
        ks_irq_restore(access->SavedIf);
        return 0;
    }
    return 1;
}

// ====== kphys_leave ======

void kphys_leave(KPHYS_ACCESS *access) {
    if (!kernel_lock_release(access->Context, &access->Guard)) ks_fail_stop("PHYSICAL_LOCK_RELEASE_FAILED");
    ks_irq_restore(access->SavedIf);
}

// ====== kphys_buffer ======

int kphys_buffer(const KERNEL_PHYSICAL_ALLOCATOR *owner, const KPHYS_ACCESS *access,
    const void *buffer, size_t bytes, int writable) {
    if (!ks_span(buffer, bytes) ||
        (writable && ks_overlap(buffer, bytes, owner, sizeof(*owner))) ||
        ks_overlap(buffer, bytes, access, sizeof(*access)) ||
        ks_overlap(buffer, bytes, access->Context, sizeof(*access->Context))
    )
        return 0;
    for (uint32_t index = 0; index < access->Context->Depth; ++index) {
        const KERNEL_LOCK_FRAME *frame = &access->Context->Frames[index];
        if (!ks_span((const void *)(uintptr_t)frame->Guard, sizeof(KERNEL_LOCK_GUARD)) ||
            !ks_span((const void *)(uintptr_t)frame->Lock, sizeof(KERNEL_LOCK)) ||
            ks_overlap(buffer, bytes, (const void *)(uintptr_t)frame->Guard, sizeof(KERNEL_LOCK_GUARD)) ||
            ks_overlap(buffer, bytes, (const void *)(uintptr_t)frame->Lock, sizeof(KERNEL_LOCK))
        )
            return 0;
    }
    return !access->Context->Wait ||
        !ks_overlap(buffer, bytes, access->Context->Wait, sizeof(*access->Context->Wait));
}
