#include "internal.h"

// ====== kernel_heap_init ======

int kernel_heap_init(KERNEL_HEAP *heap, void *base, uint32_t pages) {
    uint64_t saved_if = ks_irq_save();
    KERNEL_LOCK_CONTEXT *context = kernel_lock_current_context();
    uintptr_t address = (uintptr_t)base;
    int result = 0;
    if (context && !context->Depth && ks_span(heap, sizeof(*heap)) && address && !(address & 4095) &&
        pages && pages <= KERNEL_HEAP_MAX_PAGES && address <= UINTPTR_MAX - (uintptr_t)pages * 4096 &&
        !ks_overlap(heap, sizeof(*heap), base, (size_t)pages * 4096) &&
        !ks_overlap(heap, sizeof(*heap), context, sizeof(*context)) &&
        !ks_overlap(base, (size_t)pages * 4096, context, sizeof(*context)) &&
        (!context->Wait || (!ks_overlap(heap, sizeof(*heap), context->Wait, sizeof(*context->Wait)) &&
            !ks_overlap(base, (size_t)pages * 4096, context->Wait, sizeof(*context->Wait)))))
        result = kh_initialize(heap, address, pages);
    ks_irq_restore(saved_if);
    return result;
}

// ====== kernel_heap_grow ======

int kernel_heap_grow(KERNEL_HEAP *heap, uint32_t additional_pages) {
    KH_ACCESS access = {0};
    if (!additional_pages || !kh_enter(heap, &access)) return 0;
    int result = 0;
    if (kh_shape(heap) && additional_pages <= KERNEL_HEAP_MAX_PAGES - heap->Pages) {
        uint32_t next_pages = heap->Pages + additional_pages;
        if (heap->Base <= UINTPTR_MAX - (uintptr_t)next_pages * 4096 &&
            !ks_overlap(heap, sizeof(*heap), (void *)heap->Base, (size_t)next_pages * 4096) &&
            !ks_overlap((void *)heap->Base, (size_t)next_pages * 4096, access.Context, sizeof(*access.Context))) {
            heap->Pages = next_pages;
            heap->Used.Bits = next_pages;
            heap->Stats.FreePages += additional_pages;
            result = 1;
        }
    }
    kh_leave(&access);
    return result;
}

// ====== kernel_heap_valid ======

int kernel_heap_valid(const KERNEL_HEAP *heap) {
    KH_ACCESS access = {0};
    if (!kh_enter(heap, &access)) return 0;
    int result = kh_validate(heap);
    kh_leave(&access);
    return result;
}

// ====== transfer_private ======

static int transfer_private(KERNEL_HEAP *heap, KH_TRANSFER *transfer) {
    kh_fill(heap, transfer);
    KH_ACCESS access = {0};
    if (!kh_enter(heap, &access) || !kh_commit(heap, transfer)) ks_fail_stop("HEAP_TRANSFER_COMMIT_FAILED");
    kh_leave(&access);
    return 1;
}

// ====== allocate_private ======

static void *allocate_private(KERNEL_HEAP *heap, uint64_t bytes, uint64_t alignment, int pages) {
    KH_ACCESS access = {0};
    KH_TRANSFER transfer = {0};
    if (!kh_enter(heap, &access)) return 0;
    int reserved = kh_reserve(heap, bytes, alignment, pages, &transfer);
    kh_leave(&access);
    if (!reserved) return 0;
    return transfer_private(heap, &transfer) ? (void *)transfer.Address : 0;
}

// ====== kernel_heap_alloc ======

void *kernel_heap_alloc(KERNEL_HEAP *heap, uint64_t bytes, uint64_t alignment) {
    return allocate_private(heap, bytes, alignment ? alignment : 16, 0);
}

// ====== kernel_heap_pages ======

void *kernel_heap_pages(KERNEL_HEAP *heap, uint32_t count) {
    return allocate_private(heap, (uint64_t)count * 4096, 4096, 1);
}

// ====== kernel_heap_free ======

int kernel_heap_free(KERNEL_HEAP *heap, void *pointer) {
    KH_ACCESS access = {0};
    KH_TRANSFER transfer = {0};
    if (!kh_enter(heap, &access)) return 0;
    int reserved = kh_retire(heap, pointer, &transfer);
    kh_leave(&access);
    return reserved ? transfer_private(heap, &transfer) : 0;
}

// ====== kernel_heap_stats ======

KERNEL_HEAP_STATS kernel_heap_stats(const KERNEL_HEAP *heap) {
    KH_ACCESS access = {0};
    KERNEL_HEAP_STATS stats = {0};
    if (!kh_enter(heap, &access)) return stats;
    if (kh_shape(heap)) stats = heap->Stats;
    kh_leave(&access);
    return stats;
}
