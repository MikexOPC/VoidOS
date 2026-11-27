#ifndef OS_KERNEL_HEAP_H
#define OS_KERNEL_HEAP_H

#include <stdint.h>
#include "lib/containers/bitmap.h"
#include "kernel/sync/lock.h"

// G3 H2-A: kernel heap core over one page-aligned virtual range, independent of paging (the x86
// part maps the range, see arch/x86/mm/heap). Small objects come from size-class slabs, larger
// ones from contiguous page runs (first fit). Fixed metadata, no allocation of its own.
// Metadata serialized per owner; backing remains pinned. Never from NMI/fault or IRQ context.
#define KERNEL_HEAP_MAX_PAGES   24576U      // 96 MiB ceiling (scaled to available RAM at boot)
#define KERNEL_HEAP_SLAB_PAGES  1536U       // at most 6 MiB of small objects
#define KERNEL_HEAP_CLASSES     8U          // 16, 32, ... 2048 bytes
#define KERNEL_HEAP_CANARY      0x4b48454150434e59ULL
#define KERNEL_HEAP_POISON      0xccU

// Page kinds: free, first page of a run, continuation of a run, slab page.
#define KERNEL_HEAP_PAGE_FREE   0U
#define KERNEL_HEAP_PAGE_RUN    1U
#define KERNEL_HEAP_PAGE_TAIL   2U
#define KERNEL_HEAP_PAGE_SLAB   3U

typedef struct {
    uint64_t  FreePages;
    uint64_t  RunPages;
    uint64_t  SlabPages;
    uint64_t  Objects;
    uint64_t  Rejected;       // invalid or double free, corrupted canary
    uint64_t  Pending;        // reserved objects being cleared or retired
} KERNEL_HEAP_STATS;

typedef struct {
    uintptr_t   Self;
    uintptr_t   Base;
    uint32_t    Pages;
    uint32_t    Ready;
    uint64_t    UsedWords[KERNEL_HEAP_MAX_PAGES / 64];
    OS_BITMAP   Used;
    uint8_t     Kind[KERNEL_HEAP_MAX_PAGES];
    uint32_t    RunLength[KERNEL_HEAP_MAX_PAGES];     // pages of the run starting here
    uint16_t    SlabOf[KERNEL_HEAP_MAX_PAGES];        // slab index + 1 of a slab page
    uint32_t    SlabPage[KERNEL_HEAP_SLAB_PAGES];     // heap page of each slab
    uint8_t     SlabClass[KERNEL_HEAP_SLAB_PAGES];    // class index, 0xff = unused slab
    uint16_t    SlabFree[KERNEL_HEAP_SLAB_PAGES];     // free slots
    uint64_t    SlotWords[KERNEL_HEAP_SLAB_PAGES][4]; // 1 = slot in use (up to 256 slots)
    KERNEL_HEAP_STATS  Stats;
    uint64_t    BusyWords[KERNEL_HEAP_SLAB_PAGES][4];
    KERNEL_LOCK Gate;
} KERNEL_HEAP;

int       kernel_heap_init(KERNEL_HEAP *heap, void *base, uint32_t pages);
int       kernel_heap_grow(KERNEL_HEAP *heap, uint32_t additional_pages);
// bytes 1..; alignment a power of two up to 4096 (0 = 16). Zeroed memory or 0.
void     *kernel_heap_alloc(KERNEL_HEAP *heap, uint64_t bytes, uint64_t alignment);
// count contiguous pages, page aligned and zeroed, or 0.
void     *kernel_heap_pages(KERNEL_HEAP *heap, uint32_t count);
// 1 freed; 0 rejected (not an allocation, already free, or a corrupted canary: Rejected counts it).
int       kernel_heap_free(KERNEL_HEAP *heap, void *pointer);
int       kernel_heap_valid(const KERNEL_HEAP *heap);
KERNEL_HEAP_STATS  kernel_heap_stats(const KERNEL_HEAP *heap);

#endif
