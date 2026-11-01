#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "kernel/mm/heap/internal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static KERNEL_HEAP heap;
static __declspec(align(4096)) unsigned char arena[64 * 4096];
static HANDLE reached, resume;
static LONG armed;
static uint32_t small_mode, release_mode;
static void *pointer, *result;
static KH_TRANSFER observed;

// ====== kh_test_fill ======

void kh_test_fill(KERNEL_HEAP *owner, const KH_TRANSFER *transfer, int after) {
    assert(owner == &heap && !kernel_lock_current_context()->Depth && ks_irq_state() == 0x200);
    if (after || InterlockedCompareExchange(&armed, 0, 1) != 1) return;
    observed = *transfer;
    assert(SetEvent(reached) && WaitForSingleObject(resume, 10000) == WAIT_OBJECT_0);
}

// ====== worker_private ======

static DWORD WINAPI worker_private(void *ignored) {
    (void)ignored;
    if (release_mode) assert(kernel_heap_free(&heap, pointer));
    else result = small_mode ? kernel_heap_alloc(&heap, 24, 0) : kernel_heap_pages(&heap, 2);
    assert(ks_irq_state() == 0x200 && !kernel_lock_current_context()->Depth);
    return 0;
}

// ====== scenario_private ======

static void scenario_private(uint32_t small, uint32_t release) {
    memset(&heap, 0, sizeof(heap)); memset(arena, 0xa5, sizeof(arena));
    assert(kernel_heap_init(&heap, arena, 64));
    small_mode = small; release_mode = release; result = NULL;
    reached = CreateEventW(NULL, TRUE, FALSE, NULL); resume = CreateEventW(NULL, TRUE, FALSE, NULL);
    assert(reached && resume);
    pointer = release ? (small ? kernel_heap_alloc(&heap, 24, 0) : kernel_heap_pages(&heap, 2)) : NULL;
    assert(!release || pointer);
    InterlockedExchange(&armed, 1);
    HANDLE thread = CreateThread(NULL, 0, worker_private, NULL, 0, NULL);
    assert(thread && WaitForSingleObject(reached, 10000) == WAIT_OBJECT_0);
    assert(observed.Small == small && observed.Release == release);
    KERNEL_HEAP_STATS stats = kernel_heap_stats(&heap);
    assert(stats.Pending == 1 && stats.Objects == 1 && kernel_heap_valid(&heap));
    assert(!kernel_heap_init(&heap, arena, 64));
    assert(!kernel_heap_free(&heap, (void *)observed.Address));
    unsigned char *other = kernel_heap_alloc(&heap, 24, 0);
    assert(other);
    assert((uintptr_t)other >= observed.Address + observed.Bytes || (uintptr_t)other + 24 <= observed.Address);
    memset(other, 0x5a, 24);
    assert(kernel_heap_free(&heap, other));
    assert(kernel_heap_stats(&heap).Pending == 1 && kernel_heap_valid(&heap));
    assert(SetEvent(resume) && WaitForSingleObject(thread, 10000) == WAIT_OBJECT_0);
    DWORD code;
    assert(GetExitCodeThread(thread, &code) && !code && CloseHandle(thread));
    assert(CloseHandle(reached) && CloseHandle(resume));
    stats = kernel_heap_stats(&heap);
    assert(!stats.Pending && kernel_heap_valid(&heap));
    if (!release) {
        assert(result && (uintptr_t)result == observed.Address);
        for (uint32_t index = 0; index < (small ? 24U : 8192U); ++index) assert(!((unsigned char *)result)[index]);
        assert(kernel_heap_free(&heap, result));
    } else for (uint32_t index = 0; index < observed.Bytes; ++index) assert(((unsigned char *)pointer)[index] == KERNEL_HEAP_POISON);
    stats = kernel_heap_stats(&heap);
    assert(stats.FreePages == 64 && !stats.Objects && !stats.Pending && !stats.RunPages && !stats.SlabPages);
}

// ====== main ======

int main(void) {
    for (uint32_t repetition = 0; repetition < 10; ++repetition)
        for (uint32_t small = 0; small < 2; ++small)
            for (uint32_t release = 0; release < 2; ++release) scenario_private(small, release);
    puts("HEAP_TRANSFER_PASS: 40 suspended transfers, run/slab init/retire, other allocator progresses, busy free rejected, no premature reuse");
    return 0;
}
