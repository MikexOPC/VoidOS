#include "../internal.h"

// ====== kh_shape ======

int kh_shape(const KERNEL_HEAP *heap) {
    return heap && heap->Self == (uintptr_t)heap && heap->Ready == 1 && heap->Base && !(heap->Base & 4095) &&
        heap->Pages && heap->Pages <= KERNEL_HEAP_MAX_PAGES &&
        heap->Base <= UINTPTR_MAX - (uintptr_t)heap->Pages * 4096 &&
        !ks_overlap(heap, sizeof(*heap), (void *)heap->Base, (size_t)heap->Pages * 4096) &&
        heap->Used.Words == heap->UsedWords && heap->Used.Bits == heap->Pages &&
        heap->Stats.FreePages <= heap->Pages && heap->Stats.RunPages <= heap->Pages &&
        heap->Stats.SlabPages <= KERNEL_HEAP_SLAB_PAGES &&
        heap->Stats.FreePages + heap->Stats.RunPages + heap->Stats.SlabPages == heap->Pages &&
        heap->Stats.Objects <= (uint64_t)heap->Pages * 256 && heap->Stats.Pending <= heap->Stats.Objects;
}

// ====== kh_slab_valid ======

int kh_slab_valid(const KERNEL_HEAP *heap, uint32_t slab) {
    if (!heap->Stats.SlabPages || slab >= KERNEL_HEAP_SLAB_PAGES || heap->SlabClass[slab] >= KERNEL_HEAP_CLASSES ||
        heap->SlabPage[slab] >= heap->Pages) return 0;
    uint32_t page = heap->SlabPage[slab], slots = 4096U >> (4 + heap->SlabClass[slab]), used = 0;
    if (heap->Kind[page] != KERNEL_HEAP_PAGE_SLAB || heap->SlabOf[page] != slab + 1 ||
        heap->RunLength[page] != 1 || !os_bitmap_test(&heap->Used, page)) return 0;
    for (uint32_t word = 0; word < 4; ++word) {
        uint64_t mask = word * 64 >= slots ? 0 : slots - word * 64 >= 64 ? UINT64_MAX : (1ULL << (slots - word * 64)) - 1;
        uint64_t bits = heap->SlotWords[slab][word];
        if ((bits & ~mask) || (heap->BusyWords[slab][word] & ~bits)) return 0;
        while (bits) { bits &= bits - 1; ++used; }
    }
    return used && heap->SlabFree[slab] == slots - used;
}

// ====== kh_run_valid ======

int kh_run_valid(const KERNEL_HEAP *heap, uint32_t page, uint32_t kind) {
    if (page >= heap->Pages || heap->Kind[page] != kind || heap->SlabOf[page] ||
        !heap->RunLength[page] || heap->RunLength[page] > heap->Pages - page) return 0;
    for (uint32_t index = 0; index < heap->RunLength[page]; ++index) {
        uint32_t current = page + index;
        if (!os_bitmap_test(&heap->Used, current) || heap->SlabOf[current] ||
            (index && (heap->Kind[current] != KERNEL_HEAP_PAGE_TAIL || heap->RunLength[current]))) return 0;
    }
    return 1;
}

// ====== kh_validate ======

int kh_validate(const KERNEL_HEAP *heap) {
    if (!kh_shape(heap)) return 0;
    KERNEL_HEAP_STATS sum = {0};
    for (uint32_t page = 0; page < heap->Pages; ++page) {
        uint32_t kind = heap->Kind[page];
        if (kind == KERNEL_HEAP_PAGE_FREE) {
            if (os_bitmap_test(&heap->Used, page) || heap->RunLength[page] || heap->SlabOf[page]) return 0;
            ++sum.FreePages;
        } else if (kind == KERNEL_HEAP_PAGE_RUN || kind == KH_INITIALIZING || kind == KH_RETIRING) {
            if (!kh_run_valid(heap, page, kind)) return 0;
            sum.RunPages += heap->RunLength[page]; ++sum.Objects;
            if (kind != KERNEL_HEAP_PAGE_RUN) ++sum.Pending;
            page += heap->RunLength[page] - 1;
        } else if (kind == KERNEL_HEAP_PAGE_SLAB) {
            uint32_t slab = heap->SlabOf[page] - 1U;
            if (!kh_slab_valid(heap, slab)) return 0;
            ++sum.SlabPages;
            sum.Objects += (4096U >> (4 + heap->SlabClass[slab])) - heap->SlabFree[slab];
            for (uint32_t word = 0; word < 4; ++word) {
                uint64_t bits = heap->BusyWords[slab][word];
                while (bits) { bits &= bits - 1; ++sum.Pending; }
            }
        } else return 0;
    }
    for (uint32_t slab = 0; slab < KERNEL_HEAP_SLAB_PAGES; ++slab) {
        if (heap->SlabClass[slab] == 0xff) {
            if (heap->SlabPage[slab] || heap->SlabFree[slab]) return 0;
            for (uint32_t word = 0; word < 4; ++word)
                if (heap->SlotWords[slab][word] || heap->BusyWords[slab][word]) return 0;
        } else if (!kh_slab_valid(heap, slab)) return 0;
    }
    for (uint32_t page = heap->Pages; page < KERNEL_HEAP_MAX_PAGES; ++page)
        if (os_bitmap_test(&(OS_BITMAP){(uint64_t *)heap->UsedWords, KERNEL_HEAP_MAX_PAGES}, page) ||
            heap->Kind[page] || heap->RunLength[page] || heap->SlabOf[page]) return 0;
    return sum.FreePages == heap->Stats.FreePages && sum.RunPages == heap->Stats.RunPages &&
        sum.SlabPages == heap->Stats.SlabPages && sum.Objects == heap->Stats.Objects && sum.Pending == heap->Stats.Pending;
}

// ====== kh_initialize ======

int kh_initialize(KERNEL_HEAP *heap, uintptr_t base, uint32_t pages) {
    for (uint32_t index = 0; index < sizeof(*heap); ++index)
        if (((const unsigned char *)heap)[index]) return 0;
    for (uint32_t slab = 0; slab < KERNEL_HEAP_SLAB_PAGES; ++slab) heap->SlabClass[slab] = 0xff;
    if (!os_bitmap_init(&heap->Used, heap->UsedWords, pages)) ks_fail_stop("HEAP_PRIVATE_INIT_FAILED");
    heap->Base = base; heap->Pages = pages;
    heap->Stats.FreePages = pages;
    if (!kernel_lock_initialize(&heap->Gate, KH_RANK)) ks_fail_stop("HEAP_LOCK_INITIALIZATION_FAILED");
    heap->Self = (uintptr_t)heap; heap->Ready = 1;
    return 1;
}
