#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "test.h"
#define ITERATIONS 10000U
static KERNEL_LOCK shared, nested;
static uint64_t sequence, payload[16];
static HANDLE start_event;
static KERNEL_LOCK_WAIT wait;

// ====== worker_private ======

static DWORD WINAPI worker_private(void *parameter) {
    KERNEL_LOCK_CONTEXT context = {0};
    KERNEL_LOCK_GUARD guard = {0}, inner = {0};
    (void)parameter;
    assert(kernel_lock_context_initialize(&context, &wait));
    assert(WaitForSingleObject(start_event, 10000) == WAIT_OBJECT_0);
    for (uint32_t i = 0; i < ITERATIONS; ++i) {
        assert(ks_irq_state() == 0x200);
        assert(kernel_lock_acquire(&shared, &context, &guard));
        assert(!ks_irq_state() && guard.Ticket == sequence);
        for (uint32_t word = 0; word < 16; ++word) assert(payload[word] == sequence);
        ++sequence;
        for (uint32_t word = 0; word < 16; ++word) payload[word] = sequence;
        if (!(i % 23)) {
            assert(kernel_lock_acquire(&nested, &context, &inner));
            assert(kernel_lock_release(&context, &inner) && !ks_irq_state());
        }
        assert(kernel_lock_release(&context, &guard));
    }
    assert(!context.Depth && ks_irq_state() == 0x200);
    return 0;
}

// ====== test_threads ======

void test_threads(uint32_t count) {
    HANDLE threads[8] = {0};
    DWORD handles_before, handles_after;
    uint32_t owned_handles = 0;
    assert(count && count <= 8);
    assert(GetProcessHandleCount(GetCurrentProcess(), &handles_before));
    assert(kernel_lock_initialize(&shared, 10) && kernel_lock_initialize(&nested, 20));
    wait = (KERNEL_LOCK_WAIT){{test_clock, NULL, UINT32_MAX, 1000}, 5000, 10000000};
    start_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    assert(start_event);
    ++owned_handles;
    for (uint32_t i = 0; i < count; ++i) {
        threads[i] = CreateThread(NULL, 0, worker_private, NULL, 0, NULL);
        assert(threads[i]);
        ++owned_handles;
    }
    assert(SetEvent(start_event));
    assert(WaitForMultipleObjects(count, threads, TRUE, 60000) == WAIT_OBJECT_0);
    for (uint32_t i = 0; i < count; ++i) {
        DWORD code;
        assert(GetExitCodeThread(threads[i], &code) && code == 0);
        assert(CloseHandle(threads[i]));
        threads[i] = NULL;
        --owned_handles;
    }
    assert(CloseHandle(start_event));
    start_event = NULL;
    --owned_handles;
    assert(!owned_handles && !start_event);
    for (uint32_t i = 0; i < count; ++i) assert(!threads[i]);
    assert(GetProcessHandleCount(GetCurrentProcess(), &handles_after));
    assert(sequence == (uint64_t)count * ITERATIONS);
    assert(ks_atomic_load_acquire(&shared.Serving.Value) == sequence);
    assert(ks_atomic_load_relaxed(&shared.Next.Value) == sequence);
    printf("PASS: %u real host threads, %llu FIFO acquisitions, published payload, nested locks, owned handles closed; process handles %lu -> %lu\n",
        count, sequence, handles_before, handles_after);
}
