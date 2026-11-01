#include "../internal.h"

// ====== kh_enter ======

int kh_enter(const KERNEL_HEAP *heap, KH_ACCESS *access) {
    access->SavedIf = ks_irq_save();
    access->Context = kernel_lock_current_context();
    if (!access->Context || !ks_span(heap, sizeof(*heap)) || heap->Self != (uintptr_t)heap ||
        heap->Ready != 1 || heap->Gate.Rank != KH_RANK ||
        !kernel_lock_acquire((KERNEL_LOCK *)&heap->Gate, access->Context, &access->Guard)) {
        ks_irq_restore(access->SavedIf);
        return 0;
    }
    return 1;
}

// ====== kh_leave ======

void kh_leave(KH_ACCESS *access) {
    if (!kernel_lock_release(access->Context, &access->Guard)) ks_fail_stop("HEAP_LOCK_RELEASE_FAILED");
    ks_irq_restore(access->SavedIf);
}
