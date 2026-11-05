#include "arch/x86/kernel/scheduler/admission/admission.h"
#include <assert.h>
#include <windows.h>
#include <stdio.h>

// ====== consumer_private ======

static DWORD WINAPI consumer_private(void *argument)
{
    uint32_t cpu = (uint32_t)(uintptr_t)argument;
    if (xad_claim(cpu,cpu,0x1000) != cpu || !xad_admit(cpu,cpu,cpu,0x1000)) return 1;
    for (uint32_t step = 0; step < 100000; ++step)
        if (xad_claim(cpu,cpu,0x1000) || xad_admit(cpu,cpu,cpu,0x1000) ||
            xad_value(cpu,0x1000) != ((uint64_t)cpu << 3 | XAD_ADMITTED)) return 2;
    return 0;
}

// ====== main ======

int main(void)
{
    HANDLE workers[3];
    assert(!xad_configure(4,0x1000,1));
    assert(xad_configure(4,0x1000,0));
    for (uint32_t cpu = 1; cpu < 4; ++cpu) {
        assert(!xad_offer(cpu,0,0,0x1000));
        assert(!xad_offer(cpu,cpu,cpu,0x1000));
        assert(xad_claim(cpu,0,0x1000) == XAD_INVALID);
        assert(xad_offer(cpu,0,cpu,0x1000));
        workers[cpu - 1] = CreateThread(NULL,0,consumer_private,(void *)(uintptr_t)cpu,0,NULL);
        assert(workers[cpu - 1]);
    }
    assert(WaitForMultipleObjects(3,workers,TRUE,30000) == WAIT_OBJECT_0);
    for (uint32_t cpu = 1; cpu < 4; ++cpu) {
        DWORD code = 99;
        assert(GetExitCodeThread(workers[cpu - 1],&code) && !code);
        assert(CloseHandle(workers[cpu - 1]));
        assert(xad_fail(cpu,0,cpu,0x1000));
        assert(!xad_admit(cpu,cpu,cpu,0x1000));
        assert(xad_value(cpu,0x1000) == ((uint64_t)cpu << 3 | XAD_FAILED));
    }
    puts("ADMISSION_FFI_PASS: three concurrent consumers, 300000 repeated claims rejected, handles closed");
    return 0;
}
