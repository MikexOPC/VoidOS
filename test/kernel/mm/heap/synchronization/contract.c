#include "kernel/mm/heap/internal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static KERNEL_HEAP heap;
static __declspec(align(4096)) unsigned char bytes[4096 * 32];
static __declspec(align(4096)) unsigned char maximum[KERNEL_HEAP_MAX_PAGES * 4096ULL];
void physical_test_context_allowed(int allowed);

// ====== check_empty_private ======

static void check_empty_private(void) {
    KERNEL_HEAP_STATS stats = kernel_heap_stats(&heap);
    assert(kernel_heap_valid(&heap) && stats.FreePages == 32 && !stats.Objects && !stats.Pending && !stats.RunPages && !stats.SlabPages);
}

// ====== failed_commit_private ======

static void failed_commit_private(int small, int release) {
    KH_TRANSFER transfer = {0}, invalid;
    KH_ACCESS access = {0};
    void *pointer = release ? kernel_heap_alloc(&heap, small ? 24 : 8192, 0) : NULL;
    assert(!release || pointer);
    assert(kh_enter(&heap, &access));
    assert(release ? kh_retire(&heap, pointer, &transfer) : kh_reserve(&heap, small ? 24 : 8192, 16, 0, &transfer));
    kh_leave(&access);
    kh_fill(&heap, &transfer);
    invalid = transfer; ++invalid.Bytes;
    access = (KH_ACCESS){0};
    assert(kh_enter(&heap, &access));
    KERNEL_HEAP_STATS before = heap.Stats;
    assert(!kh_commit(&heap, &invalid));
    assert(!memcmp(&before, &heap.Stats, sizeof(before)) && kh_validate(&heap));
    invalid = transfer; invalid.Release = 2;
    assert(!kh_commit(&heap, &invalid) && heap.Stats.Pending == 1);
    assert(kh_commit(&heap, &transfer));
    kh_leave(&access);
    if (!release) assert(kernel_heap_free(&heap, (void *)transfer.Address));
    check_empty_private();
}

// ====== main ======

int main(void) {
    KERNEL_LOCK_CONTEXT *context = kernel_lock_current_context();
    assert(context && ks_irq_state() == 0x200);
    assert(!kernel_heap_init((KERNEL_HEAP *)(bytes + 1), bytes, 1));
    assert(!kernel_heap_init((KERNEL_HEAP *)bytes, bytes, 32));
    assert(!kernel_heap_init(&heap, (void *)(UINTPTR_MAX & ~4095ULL), 2));
    physical_test_context_allowed(0);
    assert(!kernel_heap_init(&heap, bytes, 32));
    physical_test_context_allowed(1);
    assert(kernel_heap_init(&heap, bytes, 32));
    assert(!kernel_heap_init(&heap, bytes, 32));
    assert(!kernel_heap_alloc(&heap, UINT64_MAX, 0));
    assert(!kernel_heap_alloc(&heap, UINT64_MAX - 7, 4096));
    assert(!kernel_heap_pages(&heap, UINT32_MAX));
    check_empty_private();
    (void)ks_irq_save();
    void *small = kernel_heap_alloc(&heap, 24, 0);
    assert(small && !ks_irq_state() && kernel_heap_free(&heap, small) && !ks_irq_state());
    ks_irq_restore(0x200);
    assert(kernel_heap_alloc(&heap, 24, 0) == small);
    uint32_t page = (uint32_t)(((uintptr_t)small - heap.Base) / 4096), slab = heap.SlabOf[page] - 1;
    uint8_t klass = heap.SlabClass[slab];
    heap.SlabClass[slab] = 255;
    assert(!kernel_heap_free(&heap, small) && !kernel_heap_valid(&heap));
    heap.SlabClass[slab] = klass;
    uint16_t owner = heap.SlabOf[page]; heap.SlabOf[page] = UINT16_MAX;
    assert(!kernel_heap_free(&heap, small) && !kernel_heap_valid(&heap));
    heap.SlabOf[page] = owner;
    assert(kernel_heap_free(&heap, small));
    void *run = kernel_heap_pages(&heap, 3);
    assert(run);
    page = (uint32_t)(((uintptr_t)run - heap.Base) / 4096);
    heap.RunLength[page] = UINT32_MAX;
    assert(!kernel_heap_free(&heap, run) && !kernel_heap_valid(&heap));
    heap.RunLength[page] = 3;
    assert(kernel_heap_free(&heap, run));
    check_empty_private();
    KERNEL_LOCK_GUARD guard = {0};
    assert(kernel_lock_acquire(&heap.Gate, context, &guard));
    assert(!kernel_heap_valid(&heap) && !kernel_heap_alloc(&heap, 1, 0));
    assert(!kernel_heap_free(&heap, bytes) && !kernel_heap_stats(&heap).FreePages);
    assert(!ks_irq_state() && context->Depth == 1);
    assert(kernel_lock_release(context, &guard) && ks_irq_state() == 0x200);
    physical_test_context_allowed(0);
    assert(!kernel_heap_alloc(&heap, 1, 0) && !kernel_heap_valid(&heap));
    assert(!kernel_heap_free(&heap, bytes) && !kernel_heap_stats(&heap).FreePages);
    physical_test_context_allowed(1);
    check_empty_private();
    for (int small_mode = 0; small_mode < 2; ++small_mode)
        for (int release_mode = 0; release_mode < 2; ++release_mode) failed_commit_private(small_mode, release_mode);
    void *all = kernel_heap_pages(&heap, 32);
    assert(all == bytes && !kernel_heap_alloc(&heap, 1, 0));
    for (uint32_t index = 0; index < sizeof(bytes); ++index) assert(!bytes[index]);
    assert(kernel_heap_free(&heap, all));
    for (uint32_t index = 0; index < sizeof(bytes); ++index) assert(bytes[index] == KERNEL_HEAP_POISON);
    check_empty_private();
    assert(!context->Depth && ks_irq_state() == 0x200);
    memset(&heap, 0, sizeof(heap));
    memset(maximum, 0xa5, sizeof(maximum));
    assert(kernel_heap_init(&heap, maximum, KERNEL_HEAP_MAX_PAGES));
    all = kernel_heap_pages(&heap, KERNEL_HEAP_MAX_PAGES);
    assert(all == maximum && !kernel_heap_alloc(&heap, 1, 0));
    for (uint32_t index = 0; index < sizeof(maximum); ++index) assert(!maximum[index]);
    assert(kernel_heap_free(&heap, all));
    for (uint32_t index = 0; index < sizeof(maximum); ++index) assert(maximum[index] == KERNEL_HEAP_POISON);
    assert(kernel_heap_valid(&heap) && !kernel_heap_stats(&heap).Pending && !kernel_heap_stats(&heap).Objects);
    puts("HEAP_CONTRACT_PASS: overflow, bounds corruption, aliases, reinit rejection, context, IF0/IF1 and full range");
    return 0;
}
