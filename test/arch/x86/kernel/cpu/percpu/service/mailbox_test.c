#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include "arch/x86/kernel/cpu/percpu/service/service.h"

#define REQUESTS 5000U
static HANDLE start_event;

// ====== consumer_private ======

static DWORD WINAPI consumer_private(void *context) {
    uint32_t index = (uint32_t)(uintptr_t)context;
    assert(xis_ready(index, index) == 1);
    DWORD wait = WaitForSingleObject(start_event, 5000);
    assert(wait == WAIT_OBJECT_0);
    ULONGLONG deadline = GetTickCount64() + 10000;
    for (uint64_t expected = 1; expected <= REQUESTS; ++expected) {
        uint64_t token = 0;
        for (uint32_t poll = 0; poll < 10000000 && !token; ++poll) {
            assert(GetTickCount64() < deadline);
            token = xis_claim(index, index);
            if (!token) SwitchToThread();
        }
        assert(token == expected);
        assert(xis_complete(index, index, token) == 1);
    }
    return 0;
}

// ====== main ======

int main(void) {
    assert(xis_configure(4) == 1);
    assert(xis_configure(4) == 0);
    assert(xis_publish(4, 0) == XIS_INVALID);
    start_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    assert(start_event);
    HANDLE jobs[3];
    for (uint32_t i = 0; i < 3; ++i) {
        jobs[i] = CreateThread(NULL, 0, consumer_private, (void *)(uintptr_t)(i + 1), 0, NULL);
        assert(jobs[i]);
    }
    BOOL signaled = SetEvent(start_event); assert(signaled);
    ULONGLONG deadline = GetTickCount64() + 10000;
    for (uint32_t index = 1; index < 4; ++index) {
        for (uint32_t poll = 0; poll < 10000000 && xis_value(index, XIS_PHASE) != XIS_READY; ++poll) {
            assert(GetTickCount64() < deadline); SwitchToThread();
        }
        assert(xis_value(index, XIS_PHASE) == XIS_READY);
    }
    for (uint64_t expected = 1; expected <= REQUESTS; ++expected) {
        for (uint32_t index = 1; index < 4; ++index) {
            uint64_t token = 0;
            for (uint32_t poll = 0; poll < 10000000 && !token; ++poll) {
                assert(GetTickCount64() < deadline);
                token = xis_publish(index, 0);
                if (!token) SwitchToThread();
            }
            assert(token == expected);
        }
    }
    DWORD result = WaitForMultipleObjects(3, jobs, TRUE, 10000);
    assert(result == WAIT_OBJECT_0);
    for (uint32_t i = 0; i < 3; ++i) {
        assert(xis_value(i + 1, XIS_ACK) == REQUESTS);
        assert(xis_complete(i + 1, i + 1, REQUESTS) == 0);
        assert(xis_fail(i + 1, 0) == 1);
        assert(xis_publish(i + 1, 0) == XIS_INVALID);
        BOOL closed = CloseHandle(jobs[i]); assert(closed);
    }
    BOOL closed = CloseHandle(start_event); assert(closed);
    puts("IPI_MAILBOX_PASS: 15000 requests, three consumers, handles closed");
    return 0;
}
