#ifndef OS_KERNEL_HEAP_INTERNAL_H
#define OS_KERNEL_HEAP_INTERNAL_H
#include "heap.h"
#include "kernel/sync/internal.h"

// ====== private_heap_contract ======

#define KH_RANK 200U
#define KH_INITIALIZING 4U
#define KH_RETIRING 5U

typedef struct {
    KERNEL_LOCK_CONTEXT  *Context;
    KERNEL_LOCK_GUARD  Guard;
    uint64_t  SavedIf;
} KH_ACCESS;

typedef struct {
    uintptr_t  Address;
    uint64_t  Bytes;
    uint32_t  Page, Slab, Slot, Small, Release;
} KH_TRANSFER;

int kh_enter(const KERNEL_HEAP *heap, KH_ACCESS *access);
void kh_leave(KH_ACCESS *access);
int kh_shape(const KERNEL_HEAP *heap);
int kh_validate(const KERNEL_HEAP *heap);
int kh_slab_valid(const KERNEL_HEAP *heap, uint32_t slab);
int kh_run_valid(const KERNEL_HEAP *heap, uint32_t page, uint32_t kind);
int kh_initialize(KERNEL_HEAP *heap, uintptr_t base, uint32_t pages);
int kh_reserve(KERNEL_HEAP *heap, uint64_t bytes, uint64_t alignment, int pages, KH_TRANSFER *transfer);
int kh_retire(KERNEL_HEAP *heap, void *pointer, KH_TRANSFER *transfer);
int kh_commit(KERNEL_HEAP *heap, const KH_TRANSFER *transfer);
void kh_fill(KERNEL_HEAP *heap, const KH_TRANSFER *transfer);
#ifdef KERNEL_HEAP_TEST_FILL
void kh_test_fill(KERNEL_HEAP *heap, const KH_TRANSFER *transfer, int after);
#endif
#endif
