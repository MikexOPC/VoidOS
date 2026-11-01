#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "kernel/mm/physical/allocator.h"
#include "kernel/sync/internal.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#define ITERATIONS 1000U
#define FRAMES 4096U
static KERNEL_PHYSICAL_ALLOCATOR pool;
static LONG claims[FRAMES];
static uint64_t payload[FRAMES];
static HANDLE start_event;

// ====== worker_private ======

static DWORD WINAPI worker_private(void *parameter) {
    uint32_t identity = (uint32_t)(uintptr_t)parameter;
    uint32_t previous_generation = 0;
    assert(WaitForSingleObject(start_event, 10000) == WAIT_OBJECT_0);
    for (uint32_t iteration = 0; iteration < ITERATIONS; ++iteration) {
        KERNEL_PHYSICAL_LEASE lease = {0};
        KERNEL_PHYSICAL_STATUS status = {0};
        BOOT_U64 available;
        uint64_t cookie = ((uint64_t)identity << 32) | iteration;
        assert(ks_irq_state() == 0x200);
        assert(kernel_physical_allocate_aligned(&pool, identity % 4 + 1, 4, &lease));
        uint64_t first = lease.Base / 4096 - 1;
        assert(first < FRAMES && lease.Pages <= FRAMES - first);
        for (uint64_t page = 0; page < lease.Pages; ++page) {
            assert(InterlockedCompareExchange(&claims[first + page], (LONG)identity, 0) == 0);
            payload[first + page] = cookie;
        }
        assert(kernel_physical_pin(&pool, &lease, 1));
        assert(kernel_physical_is_pinned(&pool, &lease));
        assert(kernel_physical_lease_state(&pool, &lease, 1));
        assert(kernel_physical_range_pinned(&pool, lease.Base, lease.Pages));
        assert(kernel_physical_budget_read(&pool, &available) && available < FRAMES);
        assert(!kernel_physical_range_unowned(&pool, lease.Base, lease.Pages));
        assert(kernel_physical_status(&pool, &status));
        assert(kernel_physical_generation_read(&pool, &available) && available >= status.Generation);
        assert(status.Owner == (uintptr_t)&pool && status.FreePages + status.LivePages == FRAMES);
        assert(status.PinnedPages <= status.LivePages && status.Generation >= previous_generation);
        previous_generation = status.Generation;
        assert(!kernel_physical_release(&pool, &lease));
        for (uint64_t page = 0; page < lease.Pages; ++page) {
            assert(payload[first + page] == cookie);
            assert(InterlockedCompareExchange(&claims[first + page], 0, (LONG)identity) == (LONG)identity);
        }
        assert(kernel_physical_release_after_unmap(&pool, &lease));
        assert(!lease.Id && ks_irq_state() == 0x200);
    }
    assert(!kernel_lock_current_context()->Depth);
    return 0;
}

// ====== main ======

int main(int argc, char **argv) {
    unsigned char map[48] = {0};
    HANDLE threads[8] = {0};
    uint32_t count = argc == 2 ? (uint32_t)strtoul(argv[1], NULL, 10) : 0;
    KERNEL_PHYSICAL_STATUS status = {0};
    assert(count && count <= 8);
    map[0] = 7; map[9] = 0x10; map[25] = 0x10; map[32] = 8;
    assert(kernel_physical_initialize(&pool, map, sizeof(map), sizeof(map), 32));
    start_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    assert(start_event);
    for (uint32_t index = 0; index < count; ++index) {
        threads[index] = CreateThread(NULL, 0, worker_private, (void *)(uintptr_t)(index + 1), 0, NULL);
        assert(threads[index]);
    }
    assert(SetEvent(start_event));
    assert(WaitForMultipleObjects(count, threads, TRUE, 60000) == WAIT_OBJECT_0);
    for (uint32_t index = 0; index < count; ++index) {
        DWORD code;
        assert(GetExitCodeThread(threads[index], &code) && !code && CloseHandle(threads[index]));
    }
    assert(CloseHandle(start_event));
    assert(kernel_physical_status(&pool, &status));
    assert(status.FreePages == FRAMES && !status.LivePages && !status.PinnedPages && status.Count == 1);
    assert(status.Generation == count * ITERATIONS * 4 && status.NextId == (uint64_t)count * ITERATIONS + 1);
    for (uint32_t frame = 0; frame < FRAMES; ++frame) assert(!claims[frame]);
    assert(ks_atomic_load_acquire(&pool.Gate.Serving.Value) == ks_atomic_load_relaxed(&pool.Gate.Next.Value));
    printf("PHYSICAL_THREADS_PASS: %u CPUs modeled by host threads, %u lease cycles, no overlap, budget and pins restored; owned handles closed\n",
        count, count * ITERATIONS);
    return 0;
}
