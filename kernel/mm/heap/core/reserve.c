#include "../internal.h"

// ====== take_private ======

static int take_private(KERNEL_HEAP *heap, uint32_t count, uint32_t kind, uint32_t *first) {
    if (!count || count > heap->Pages || heap->Stats.FreePages < count) return 0;
    uint32_t length = 0;
    for (uint32_t page = 0; page < heap->Pages; ++page) {
        if (!os_bitmap_test(&heap->Used, page) && heap->Kind[page] == KERNEL_HEAP_PAGE_FREE &&
            !heap->RunLength[page] && !heap->SlabOf[page]) ++length;
        else length = 0;
        if (length != count) continue;
        *first = page + 1 - count;
        for (uint32_t index = 0; index < count; ++index) {
            (void)os_bitmap_set(&heap->Used, *first + index);
            heap->Kind[*first + index] = index ? KERNEL_HEAP_PAGE_TAIL : (uint8_t)kind;
        }
        heap->RunLength[*first] = count; heap->Stats.FreePages -= count;
        return 1;
    }
    return 0;
}

// ====== slab_private ======

static int slab_private(KERNEL_HEAP *heap, uint32_t klass, uint32_t *slab) {
    uint32_t unused = KERNEL_HEAP_SLAB_PAGES;
    for (uint32_t index = 0; index < KERNEL_HEAP_SLAB_PAGES; ++index) {
        if (heap->SlabClass[index] == klass && heap->SlabFree[index]) {
            if (!kh_slab_valid(heap, index)) return 0;
            *slab = index; return 1;
        }
        if (heap->SlabClass[index] == 0xff && unused == KERNEL_HEAP_SLAB_PAGES) unused = index;
    }
    if (unused == KERNEL_HEAP_SLAB_PAGES || heap->SlabPage[unused] || heap->SlabFree[unused]) return 0;
    for (uint32_t word = 0; word < 4; ++word)
        if (heap->SlotWords[unused][word] || heap->BusyWords[unused][word]) return 0;
    uint32_t page;
    if (!take_private(heap, 1, KERNEL_HEAP_PAGE_SLAB, &page)) return 0;
    heap->SlabPage[unused] = page; heap->SlabClass[unused] = (uint8_t)klass;
    heap->SlabFree[unused] = (uint16_t)(4096U >> (4 + klass));
    heap->SlabOf[page] = (uint16_t)(unused + 1);
    ++heap->Stats.SlabPages;
    *slab = unused;
    return 1;
}

// ====== kh_reserve ======

int kh_reserve(KERNEL_HEAP *heap, uint64_t bytes, uint64_t alignment, int pages, KH_TRANSFER *transfer) {
    if (!kh_shape(heap) || !bytes || bytes > (uint64_t)heap->Pages * 4096 ||
        alignment > 4096 || !alignment || (alignment & (alignment - 1))) return 0;
    uint32_t klass = 0;
    while (klass < KERNEL_HEAP_CLASSES && (bytes > (16ULL << klass) - 8 || alignment > (16ULL << klass))) ++klass;
    if (pages || klass == KERNEL_HEAP_CLASSES) {
        uint32_t count = (uint32_t)((bytes + 4095) / 4096), page;
        if (!take_private(heap, count, KH_INITIALIZING, &page)) return 0;
        heap->Stats.RunPages += count;
        *transfer = (KH_TRANSFER){heap->Base + (uintptr_t)page * 4096, (uint64_t)count * 4096, page, 0, 0, 0, 0};
    } else {
        uint32_t slab;
        if (!slab_private(heap, klass, &slab)) return 0;
        uint32_t slots = 4096U >> (4 + klass), slot = 0;
        OS_BITMAP used = {heap->SlotWords[slab], slots}, busy = {heap->BusyWords[slab], slots};
        while (slot < slots && os_bitmap_test(&used, slot)) ++slot;
        if (slot == slots) ks_fail_stop("HEAP_SLAB_RESERVATION_FAILED");
        (void)os_bitmap_set(&used, slot); (void)os_bitmap_set(&busy, slot);
        --heap->SlabFree[slab];
        uint32_t page = heap->SlabPage[slab];
        uintptr_t address = heap->Base + (uintptr_t)page * 4096 + (uintptr_t)slot * (16ULL << klass);
        *transfer = (KH_TRANSFER){address, 16ULL << klass, page, slab, slot, 1, 0};
    }
    ++heap->Stats.Objects; ++heap->Stats.Pending;
    return 1;
}
