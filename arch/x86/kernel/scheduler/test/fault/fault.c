#include "../../integration/control.h"
#include "kernel/diagnostics/log.h"

#if defined(KERNEL_TEST_SCHEDULER_FAULT) && KERNEL_TEST_SCHEDULER_FAULT < 5
// ====== x86_scheduler_test_fault ======

void x86_scheduler_test_fault(uint32_t point)
{
    uint64_t address = x86_task_stack_base(0);
    if ((KERNEL_TEST_SCHEDULER_FAULT == 4) != (point == 4)) return;
    if (!kernel_log(1,"KERNEL_SCHEDULER_FAULT_READY")) kernel_scheduler_fail_stop();
#if KERNEL_TEST_SCHEDULER_FAULT == 1
    address -= 4096;
#elif KERNEL_TEST_SCHEDULER_FAULT == 2
    address += 65536;
#elif KERNEL_TEST_SCHEDULER_FAULT == 3
    *(volatile unsigned char *)(uintptr_t)address = 0xC3; // RET data; NX must prevent execution.
    ((void (*)(void))(uintptr_t)address)();
#endif
#if KERNEL_TEST_SCHEDULER_FAULT != 3
    {
        volatile unsigned char value = *(volatile unsigned char *)(uintptr_t)address;
        (void)value;
    }
#endif
    kernel_panic("SCHEDULER_EXPECTED_FAULT_MISSING");
}
#endif
