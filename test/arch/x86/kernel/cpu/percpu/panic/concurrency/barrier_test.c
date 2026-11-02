#include <windows.h>
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "barrier.h"
#include "arch/x86/kernel/cpu/percpu/panic/native/runtime.h"
XP_PANIC_NATIVE xp_panic_native;
static HANDLE start_event;
static uint32_t mode;
static jmp_buf failed;

// ====== clock_model ======

int xp_panic_hw_now(void *context, uint32_t *now) {
    assert(context == &xp_panic_native.Source);
    if (mode == 1) return 0;
    *now = mode == 2 ? 0 : (uint32_t)((GetTickCount64() * KERNEL_PM_TIMER_HZ / 1000) & 0xffffff);
    return 1;
}
void ks_pause(void) { SwitchToThread(); }
__declspec(noreturn) void xp_panic_quiet_stop(uint64_t code) {
    assert(mode && code == UINT64_C(0x4b33424152524641));
    longjmp(failed, 1);
}

// ====== producer_private ======

static DWORD WINAPI producer_private(void *context) {
    DWORD result = WaitForSingleObject(start_event, 5000);
    assert(result == WAIT_OBJECT_0);
    xp_stress_before_elect((uint32_t)(uintptr_t)context);
    return 0;
}

// ====== main ======

int main(int argc, char **argv) {
    assert(argc == 2);
    xp_panic_native.Count = 4; xp_panic_native.Source.Mask = 0xffffff;
    if (!strcmp(argv[1], "ok")) {
        HANDLE jobs[4];
        start_event = CreateEventW(NULL, TRUE, FALSE, NULL); assert(start_event);
        for (uint32_t i = 0; i < 4; ++i) { jobs[i] = CreateThread(NULL, 0, producer_private, (void *)(uintptr_t)i, 0, NULL); assert(jobs[i]); }
        BOOL signaled = SetEvent(start_event); assert(signaled);
        DWORD result = WaitForMultipleObjects(4, jobs, TRUE, 5000); assert(result == WAIT_OBJECT_0);
        for (uint32_t i = 0; i < 4; ++i) { BOOL closed = CloseHandle(jobs[i]); assert(closed); }
        BOOL closed = CloseHandle(start_event); assert(closed);
        assert(ks_atomic_load_acquire(&xp_stress_arrived) == 15 && !ks_atomic_load_acquire(&xp_stress_failed));
    } else {
        uint32_t index = 0;
        mode = !strcmp(argv[1], "clock") ? 1 : 2;
        if (!strcmp(argv[1], "invalid")) index = 4;
        else if (!strcmp(argv[1], "duplicate")) ks_atomic_store_release(&xp_stress_arrived, 1);
        else assert(!strcmp(argv[1], "clock") || !strcmp(argv[1], "stalled"));
        if (!setjmp(failed)) { xp_stress_before_elect(index); assert(0); }
        assert(ks_atomic_load_acquire(&xp_stress_failed) == 1);
    }
    printf("PANIC_BARRIER_PASS: %s; actual Rust atomics, owned handles closed\n", argv[1]);
    return 0;
}
