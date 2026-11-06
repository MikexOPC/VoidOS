#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "test.h"
#define PRODUCERS 4U
#define REQUESTS 2000U
#define MAX_POLLS 10000000U

static KERNEL_CONSOLE_REQUEST_QUEUE queue;
static KERNEL_CONSOLE_SERVICE service;
static KERNEL_VIDEO_CONSOLE view;
static HANDLE start_event;

// ====== producer ======

static DWORD WINAPI producer(void *parameter) {
    uint32_t identity = (uint32_t)(uintptr_t)parameter;
    ULONGLONG deadline = GetTickCount64() + 60000;
    requests_context_prepare();
    assert(WaitForSingleObject(start_event, 10000) == WAIT_OBJECT_0);
    for (uint32_t sequence = 0; sequence < REQUESTS; ++sequence) {
        char text[24];
        uint32_t polls = 0, result;
        int length = sprintf_s(text, sizeof(text), "%u:%u\n", identity, sequence);
        assert(length > 0 && (uint32_t)length < sizeof(text));
        do {
            ++polls;
            assert(polls < MAX_POLLS && GetTickCount64() < deadline);
            result = kernel_console_requests_push(&queue, KCR_WRITE, text, (uint32_t)length, sizeof(text));
            if (result == KCR_FULL) SwitchToThread();
        } while (result == KCR_FULL);
        assert(result == KCR_OK && ks_irq_state() == 0x200);
    }
    return 0;
}

// ====== requests_threads ======

void requests_threads(void) {
    HANDLE threads[PRODUCERS] = {0};
    uint32_t expected[PRODUCERS] = {0}, polls = 0, consumed = 0, owned_handles = 0;
    ULONGLONG deadline = GetTickCount64() + 60000;
    DWORD handles_before, handles_after;
    assert(GetProcessHandleCount(GetCurrentProcess(), &handles_before));
    assert(kernel_console_requests_initialize(&queue));
    assert(kernel_console_service_init(&service, &view, 40, 8));
    start_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    assert(start_event);
    ++owned_handles;
    for (uint32_t index = 0; index < PRODUCERS; ++index) {
        threads[index] = CreateThread(NULL, 0, producer, (void *)(uintptr_t)index, 0, NULL);
        assert(threads[index]);
        ++owned_handles;
    }
    assert(SetEvent(start_event));
    while (consumed < PRODUCERS * REQUESTS) {
        KERNEL_CONSOLE_REQUEST packet;
        uint32_t result, identity, sequence;
        char text[257] = {0};
        ++polls;
        assert(polls < MAX_POLLS && GetTickCount64() < deadline);
        result = kernel_console_requests_take(&queue, &packet);
        if (result == KCR_EMPTY) { SwitchToThread(); continue; }
        assert(result == KCR_OK && ks_irq_state() == 0x200);
        assert(packet.Operation == KCR_WRITE && packet.Length <= KCR_TEXT_LIMIT);
        memcpy(text, packet.Text, packet.Length);
        assert(sscanf_s(text, "%u:%u", &identity, &sequence) == 2);
        assert(identity < PRODUCERS && sequence == expected[identity]);
        ++expected[identity];
        assert(kernel_console_service_write(&service, text, packet.Length, sizeof(text)));
        assert(kernel_console_requests_complete(&queue, 1) == KCR_OK);
        ++consumed;
    }
    assert(WaitForMultipleObjects(PRODUCERS, threads, TRUE, 10000) == WAIT_OBJECT_0);
    for (uint32_t index = 0; index < PRODUCERS; ++index) {
        DWORD exit_code;
        assert(GetExitCodeThread(threads[index], &exit_code) && exit_code == 0);
        assert(expected[index] == REQUESTS && CloseHandle(threads[index]));
        threads[index] = NULL;
        --owned_handles;
    }
    assert(CloseHandle(start_event));
    start_event = NULL;
    --owned_handles;
    assert(!owned_handles);
    assert(kernel_console_requests_drain(&queue) == KCR_OK);
    assert(kernel_console_requests_close(&queue) == KCR_OK);
    assert(kernel_console_service_close(&service));
    assert(GetProcessHandleCount(GetCurrentProcess(), &handles_after));
    printf("PASS: %u producers, %u ordered requests, C service outside queue lock, handles %lu -> %lu\n",
        PRODUCERS, consumed, handles_before, handles_after);
}
