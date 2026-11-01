#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "kernel/mm/heap/internal.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#define PAGES 256U
#define ITERATIONS 1000U
static KERNEL_HEAP heap;
static __declspec(align(4096)) unsigned char arena[PAGES * 4096];
static LONG claims[sizeof(arena) / 16];
static HANDLE start_event;

// ====== claim_private ======

static void claim_private(void *pointer, uint64_t bytes, LONG identity, int take) {
    uint64_t first = ((uintptr_t)pointer - (uintptr_t)arena) / 16, count = (bytes + 15) / 16;
    assert(first < sizeof(claims) / sizeof(claims[0]) && count <= sizeof(claims) / sizeof(claims[0]) - first);
    for (uint64_t slot = 0; slot < count; ++slot)
        assert(InterlockedCompareExchange(&claims[first + slot], take ? identity : 0, take ? 0 : identity) == (take ? 0 : identity));
}

// ====== worker_private ======

static DWORD WINAPI worker_private(void *parameter) {
    uint32_t identity = (uint32_t)(uintptr_t)parameter;
    assert(WaitForSingleObject(start_event, 10000) == WAIT_OBJECT_0);
    for (uint32_t index = 0; index < ITERATIONS; ++index) {
        uint64_t size = index % 4 == 0 ? 8192 : 1 + (index * 37 + identity * 13) % 2040;
        unsigned char *data = index % 4 == 0 ? kernel_heap_pages(&heap, 2) : kernel_heap_alloc(&heap, size, 0);
        assert(data && ks_irq_state() == 0x200);
        claim_private(data, size, (LONG)identity, 1);
        for (uint64_t byte = 0; byte < size; ++byte) { assert(!data[byte]); data[byte] = (unsigned char)(identity | 1); }
        KERNEL_HEAP_STATS stats = kernel_heap_stats(&heap);
        assert(stats.FreePages + stats.RunPages + stats.SlabPages == PAGES && stats.Pending <= stats.Objects);
        if (!(index % 127)) assert(kernel_heap_valid(&heap));
        for (uint64_t byte = 0; byte < size; ++byte) assert(data[byte] == (unsigned char)(identity | 1));
        claim_private(data, size, (LONG)identity, 0);
        assert(kernel_heap_free(&heap, data));
        assert(!kernel_lock_current_context()->Depth && ks_irq_state() == 0x200);
    }
    return 0;
}

// ====== main ======

int main(int argc, char **argv) {
    uint32_t count = argc == 2 ? (uint32_t)strtoul(argv[1], NULL, 10) : 0;
    HANDLE threads[8];
    assert(count && count <= 8 && kernel_heap_init(&heap, arena, PAGES));
    start_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    assert(start_event);
    for (uint32_t index = 0; index < count; ++index) {
        threads[index] = CreateThread(NULL, 0, worker_private, (void *)(uintptr_t)(index + 1), 0, NULL);
        assert(threads[index]);
    }
    assert(SetEvent(start_event) && WaitForMultipleObjects(count, threads, TRUE, 60000) == WAIT_OBJECT_0);
    for (uint32_t index = 0; index < count; ++index) {
        DWORD code;
        assert(GetExitCodeThread(threads[index], &code) && !code && CloseHandle(threads[index]));
    }
    assert(CloseHandle(start_event));
    KERNEL_HEAP_STATS stats = kernel_heap_stats(&heap);
    assert(kernel_heap_valid(&heap) && stats.FreePages == PAGES && !stats.Objects && !stats.Pending && !stats.Rejected);
    for (uint32_t index = 0; index < sizeof(claims) / sizeof(claims[0]); ++index) assert(!claims[index]);
    assert(ks_atomic_load_acquire(&heap.Gate.Serving.Value) == ks_atomic_load_relaxed(&heap.Gate.Next.Value));
    printf("HEAP_THREADS_PASS: %u host threads, %u mixed cycles, independent claims and payload, zero pending, budget restored, handles closed\n",count,count * ITERATIONS);
    return 0;
}
