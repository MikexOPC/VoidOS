#include "heap.h"

// ====== fill_private ======

static void fill_private(uintptr_t address, uint64_t bytes, uint8_t value)
{
    volatile uint8_t *data = (volatile uint8_t *)address;

    for (uint64_t index = 0; index < bytes; ++index) data[index] = value;
}

// ====== kernel_heap_valid ======

int kernel_heap_valid(const KERNEL_HEAP *heap)
{
    return heap && heap->Self == (uintptr_t)heap && heap->Ready == 1 && heap->Base && !(heap->Base & 4095) &&
        heap->Pages && heap->Pages <= KERNEL_HEAP_MAX_PAGES && heap->Used.Words == heap->UsedWords &&
        heap->Used.Bits == heap->Pages;
}

// ====== kernel_heap_init ======

// The range is not touched here: the caller maps it before the first allocation.
int kernel_heap_init(KERNEL_HEAP *heap, void *base, uint32_t pages)
{
    uintptr_t address = (uintptr_t)base;

    if (!heap || !address || (address & 4095) || !pages || pages > KERNEL_HEAP_MAX_PAGES ||
        address > UINTPTR_MAX - (uintptr_t)pages * 4096) return 0;

    for (uint32_t index = 0; index < KERNEL_HEAP_MAX_PAGES; ++index) {
        heap->Kind[index] = KERNEL_HEAP_PAGE_FREE;
        heap->RunLength[index] = 0;
        heap->SlabOf[index] = 0;
    }

    for (uint32_t index = 0; index < KERNEL_HEAP_SLAB_PAGES; ++index) {
        heap->SlabPage[index] = 0;
        heap->SlabClass[index] = 0xff;
        heap->SlabFree[index] = 0;
        for (uint32_t word = 0; word < 4; ++word) heap->SlotWords[index][word] = 0;
    }

    if (!os_bitmap_init(&heap->Used, heap->UsedWords, pages)) return 0;

    heap->Base = address;
    heap->Pages = pages;
    heap->Stats = (KERNEL_HEAP_STATS){pages, 0, 0, 0, 0};
    heap->Self = (uintptr_t)heap;
    heap->Ready = 1;
    return 1;
}

// ====== run_private ======

// First fit of count free pages whose first page index is a multiple of step.
static int run_private(KERNEL_HEAP *heap, uint32_t count, uint32_t step, uint32_t *first)
{
    for (uint32_t start = 0; count <= heap->Pages && start <= heap->Pages - count; start += step) {
        uint32_t length = 0;

        while (length < count && !os_bitmap_test(&heap->Used, start + length)) ++length;

        if (length == count) { *first = start; return 1; }

        // Skip past the used page that ended the attempt, keeping the step alignment.
        start = (start + length) / step * step;
    }

    return 0;
}

// ====== take_private ======

static uintptr_t take_private(KERNEL_HEAP *heap, uint32_t count, uint32_t step, uint8_t kind)
{
    uint32_t first;

    if (!count || heap->Stats.FreePages < count || !run_private(heap, count, step, &first)) return 0;

    for (uint32_t index = 0; index < count; ++index) {
        (void)os_bitmap_set(&heap->Used, first + index);
        heap->Kind[first + index] = index ? KERNEL_HEAP_PAGE_TAIL : kind;
    }

    heap->RunLength[first] = count;
    heap->Stats.FreePages -= count;
    uintptr_t address = heap->Base + (uintptr_t)first * 4096;
    fill_private(address, (uint64_t)count * 4096, 0);
    return address;
}

// ====== kernel_heap_pages ======

void *kernel_heap_pages(KERNEL_HEAP *heap, uint32_t count)
{
    if (!kernel_heap_valid(heap)) return 0;

    uintptr_t address = take_private(heap, count, 1, KERNEL_HEAP_PAGE_RUN);

    if (address) { heap->Stats.RunPages += count; ++heap->Stats.Objects; }

    return (void *)address;
}

// ====== class_private ======

// Smallest class with room for the bytes and the trailing canary, aligned at least as asked.
static int class_private(uint64_t bytes, uint64_t alignment, uint32_t *klass)
{
    for (uint32_t index = 0; index < KERNEL_HEAP_CLASSES; ++index) {
        uint64_t size = 16ULL << index;

        if (bytes + 8 <= size && alignment <= size) { *klass = index; return 1; }
    }

    return 0;
}

// ====== slab_private ======

// A slab of the class with a free slot, or a new one on a fresh page.
static int slab_private(KERNEL_HEAP *heap, uint32_t klass, uint32_t *slab)
{
    uint32_t unused = KERNEL_HEAP_SLAB_PAGES;

    for (uint32_t index = 0; index < KERNEL_HEAP_SLAB_PAGES; ++index) {
        if (heap->SlabClass[index] == klass && heap->SlabFree[index]) { *slab = index; return 1; }
        if (heap->SlabClass[index] == 0xff && unused == KERNEL_HEAP_SLAB_PAGES) unused = index;
    }

    if (unused == KERNEL_HEAP_SLAB_PAGES) return 0;

    uintptr_t address = take_private(heap, 1, 1, KERNEL_HEAP_PAGE_SLAB);

    if (!address) return 0;

    uint32_t page = (uint32_t)((address - heap->Base) / 4096);

    heap->SlabPage[unused] = page;
    heap->SlabClass[unused] = (uint8_t)klass;
    heap->SlabFree[unused] = (uint16_t)(4096U >> (4 + klass));
    heap->SlabOf[page] = (uint16_t)(unused + 1);
    for (uint32_t word = 0; word < 4; ++word) heap->SlotWords[unused][word] = 0;
    ++heap->Stats.SlabPages;
    *slab = unused;
    return 1;
}

// ====== kernel_heap_alloc ======

void *kernel_heap_alloc(KERNEL_HEAP *heap, uint64_t bytes, uint64_t alignment)
{
    uint32_t klass, slab;

    if (!alignment) alignment = 16;

    if (!kernel_heap_valid(heap) || !bytes || alignment > 4096 || (alignment & (alignment - 1))) return 0;

    if (!class_private(bytes, alignment, &klass)) {
        if (bytes > (uint64_t)heap->Pages * 4096) return 0;
        return kernel_heap_pages(heap, (uint32_t)((bytes + 4095) / 4096));
    }

    if (!slab_private(heap, klass, &slab)) return 0;

    uint32_t size = 16U << klass, slots = 4096U >> (4 + klass), slot = 0;
    OS_BITMAP used = {heap->SlotWords[slab], slots};

    while (slot < slots && os_bitmap_test(&used, slot)) ++slot;

    if (slot == slots) return 0;

    (void)os_bitmap_set(&used, slot);
    --heap->SlabFree[slab];
    ++heap->Stats.Objects;

    uintptr_t address = heap->Base + (uintptr_t)heap->SlabPage[slab] * 4096 + (uintptr_t)slot * size;

    fill_private(address, size - 8, 0);
    *(volatile uint64_t *)(address + size - 8) = KERNEL_HEAP_CANARY;
    return (void *)address;
}

// ====== reject_private ======

static int reject_private(KERNEL_HEAP *heap)
{
    if (heap->Stats.Rejected != UINT64_MAX) ++heap->Stats.Rejected;
    return 0;
}

// ====== kernel_heap_free ======

int kernel_heap_free(KERNEL_HEAP *heap, void *pointer)
{
    uintptr_t address = (uintptr_t)pointer;

    if (!kernel_heap_valid(heap)) return 0;

    if (address < heap->Base || address - heap->Base >= (uintptr_t)heap->Pages * 4096) return reject_private(heap);

    uint32_t page = (uint32_t)((address - heap->Base) / 4096);

    if (heap->Kind[page] == KERNEL_HEAP_PAGE_RUN) {
        uint32_t count = heap->RunLength[page];

        if (address & 4095 || !count) return reject_private(heap);

        for (uint32_t index = 0; index < count; ++index) {
            (void)os_bitmap_clear(&heap->Used, page + index);
            heap->Kind[page + index] = KERNEL_HEAP_PAGE_FREE;
        }

        heap->RunLength[page] = 0;
        heap->Stats.FreePages += count;
        heap->Stats.RunPages -= count;
        --heap->Stats.Objects;
        fill_private(address, (uint64_t)count * 4096, KERNEL_HEAP_POISON);
        return 1;
    }

    if (heap->Kind[page] != KERNEL_HEAP_PAGE_SLAB || !heap->SlabOf[page]) return reject_private(heap);

    uint32_t slab = heap->SlabOf[page] - 1U, klass = heap->SlabClass[slab];
    uint32_t size = 16U << klass, offset = (uint32_t)(address & 4095);
    OS_BITMAP used = {heap->SlotWords[slab], 4096U >> (4 + klass)};

    if (offset % size || !os_bitmap_test(&used, offset / size)) return reject_private(heap);

    if (*(volatile uint64_t *)(address + size - 8) != KERNEL_HEAP_CANARY) return reject_private(heap);

    (void)os_bitmap_clear(&used, offset / size);
    ++heap->SlabFree[slab];
    --heap->Stats.Objects;
    fill_private(address, size, KERNEL_HEAP_POISON);

    // An empty slab gives its page back.
    if (heap->SlabFree[slab] == 4096U >> (4 + klass)) {
        heap->SlabClass[slab] = 0xff;
        heap->SlabFree[slab] = 0;
        heap->SlabOf[page] = 0;
        heap->Kind[page] = KERNEL_HEAP_PAGE_FREE;
        heap->RunLength[page] = 0;
        (void)os_bitmap_clear(&heap->Used, page);
        ++heap->Stats.FreePages;
        --heap->Stats.SlabPages;
    }

    return 1;
}

// ====== kernel_heap_stats ======

KERNEL_HEAP_STATS kernel_heap_stats(const KERNEL_HEAP *heap)
{
    return kernel_heap_valid(heap) ? heap->Stats : (KERNEL_HEAP_STATS){0};
}
