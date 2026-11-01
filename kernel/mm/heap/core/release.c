#include "../internal.h"

// ====== reject_private ======

static int reject_private(KERNEL_HEAP *heap) {
    if (heap->Stats.Rejected != UINT64_MAX) ++heap->Stats.Rejected;
    return 0;
}

// ====== kh_retire ======

int kh_retire(KERNEL_HEAP *heap, void *pointer, KH_TRANSFER *transfer) {
    uintptr_t address = (uintptr_t)pointer;
    if (!kh_shape(heap)) return 0;
    if (address < heap->Base || address - heap->Base >= (uintptr_t)heap->Pages * 4096) return reject_private(heap);
    uint32_t page = (uint32_t)((address - heap->Base) / 4096);
    if (!heap->Stats.Objects || heap->Stats.Pending == heap->Stats.Objects) return reject_private(heap);
    if (heap->Kind[page] == KERNEL_HEAP_PAGE_RUN) {
        if ((address & 4095) || !kh_run_valid(heap, page, KERNEL_HEAP_PAGE_RUN) ||
            heap->Stats.RunPages < heap->RunLength[page]) return reject_private(heap);
        heap->Kind[page] = KH_RETIRING;
        *transfer = (KH_TRANSFER){address, (uint64_t)heap->RunLength[page] * 4096, page, 0, 0, 0, 1};
    } else {
        if (heap->Kind[page] != KERNEL_HEAP_PAGE_SLAB || !heap->SlabOf[page]) return reject_private(heap);
        uint32_t slab = heap->SlabOf[page] - 1U;
        if (!kh_slab_valid(heap, slab)) return reject_private(heap);
        uint32_t size = 16U << heap->SlabClass[slab], offset = (uint32_t)(address & 4095), slot = offset / size;
        OS_BITMAP used = {heap->SlotWords[slab], 4096 / size}, busy = {heap->BusyWords[slab], 4096 / size};
        if (offset % size || !os_bitmap_test(&used, slot) || os_bitmap_test(&busy, slot)) return reject_private(heap);
        if (*(volatile uint64_t *)(address + size - 8) != KERNEL_HEAP_CANARY) return reject_private(heap);
        (void)os_bitmap_set(&busy, slot);
        *transfer = (KH_TRANSFER){address, size, page, slab, slot, 1, 1};
    }
    ++heap->Stats.Pending;
    return 1;
}

// ====== clear_pages_private ======

static void clear_pages_private(KERNEL_HEAP *heap, uint32_t page, uint32_t count) {
    for (uint32_t index = 0; index < count; ++index) {
        (void)os_bitmap_clear(&heap->Used, page + index);
        heap->Kind[page + index] = KERNEL_HEAP_PAGE_FREE;
    }
    heap->RunLength[page] = 0;
    heap->Stats.FreePages += count;
}

// ====== kh_commit ======

int kh_commit(KERNEL_HEAP *heap, const KH_TRANSFER *transfer) {
    if (!kh_shape(heap) || !heap->Stats.Pending || !heap->Stats.Objects || transfer->Page >= heap->Pages ||
        transfer->Small > 1 || transfer->Release > 1) return 0;
    uint32_t page = transfer->Page;
    if (transfer->Small) {
        uint32_t slab = transfer->Slab;
        if (!kh_slab_valid(heap, slab) || heap->SlabPage[slab] != page) return 0;
        uint32_t size = 16U << heap->SlabClass[slab], slots = 4096 / size;
        if (transfer->Bytes != size || transfer->Slot >= slots ||
            transfer->Address != heap->Base + (uintptr_t)page * 4096 + (uintptr_t)transfer->Slot * size) return 0;
        OS_BITMAP used = {heap->SlotWords[slab], slots}, busy = {heap->BusyWords[slab], slots};
        if (!os_bitmap_test(&used, transfer->Slot) || !os_bitmap_test(&busy, transfer->Slot)) return 0;
        (void)os_bitmap_clear(&busy, transfer->Slot);
        if (transfer->Release) {
            (void)os_bitmap_clear(&used, transfer->Slot); ++heap->SlabFree[slab];
            if (heap->SlabFree[slab] == slots) {
                heap->SlabClass[slab] = 0xff; heap->SlabFree[slab] = 0; heap->SlabPage[slab] = 0;
                heap->SlabOf[page] = 0;
                clear_pages_private(heap, page, 1); --heap->Stats.SlabPages;
            }
        }
    } else {
        uint32_t expected = transfer->Release ? KH_RETIRING : KH_INITIALIZING;
        if (!kh_run_valid(heap, page, expected) || transfer->Address != heap->Base + (uintptr_t)page * 4096 ||
            transfer->Bytes != (uint64_t)heap->RunLength[page] * 4096) return 0;
        if (transfer->Release) {
            uint32_t count = heap->RunLength[page];
            if (heap->Stats.RunPages < count) return 0;
            clear_pages_private(heap, page, count); heap->Stats.RunPages -= count;
        } else heap->Kind[page] = KERNEL_HEAP_PAGE_RUN;
    }
    --heap->Stats.Pending;
    if (transfer->Release) --heap->Stats.Objects;
    return 1;
}
