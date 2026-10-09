#include "control.h"
static uint32_t progress[8], handshake, condition;

// ====== event_workload_private ======

static void event_workload_private(void)
{
#if defined(KERNEL_TEST_SCHEDULER_FAULT) && KERNEL_TEST_SCHEDULER_FAULT == 6
    (void)x86_scheduler_event_request(4, 0, 0);
    kernel_scheduler_fail_stop(); // Both waiters have no producer; resumption forbidden.
#else
    KERNEL_EVENT_REPLY result;
    uint64_t token;
    if (x86_scheduler_index() == 0) {
        handshake = 1;
        result = x86_scheduler_event_request(3, 0, 0); token = result.epoch;
        if (result.outcome != 0 || token != 0) kernel_scheduler_fail_stop();
        result = x86_scheduler_event_request(4, 0, token);
        if (result.outcome != KERNEL_EVENT_CHANGED || result.epoch != 1) kernel_scheduler_fail_stop();
        handshake = 3;
        result = x86_scheduler_event_request(3, 1, 0); token = result.epoch;
        if (result.outcome != 0 || token != 0) kernel_scheduler_fail_stop();
        handshake = 4;
        result = x86_scheduler_event_request(4, 1, token);
        if (result.outcome != KERNEL_EVENT_WAITING || !condition || result.epoch != 0)
            kernel_scheduler_fail_stop();
        handshake = 5;
    } else {
        if (x86_scheduler_index() != 1 || handshake != 1) kernel_scheduler_fail_stop();
        result = x86_scheduler_event_request(5, 0, 0);
        if (result.outcome != 3 || result.epoch != 1 || result.woken) kernel_scheduler_fail_stop();
        handshake = 2;
        for (uint32_t retry = 0; handshake != 4; ++retry) {
            if (retry >= 4) kernel_scheduler_fail_stop();
            x86_scheduler_yield();
        }
        condition = 1;
        result = x86_scheduler_event_request(5, 1, 0);
        if (result.outcome != 3 || result.epoch != 1 || result.woken != 1 || handshake != 5)
            kernel_scheduler_fail_stop();
    }
#endif
}

// ====== workload_private ======

static void workload_private(void)
{
    uint32_t index = x86_scheduler_index(), iteration;
    volatile uint64_t locals[8];
    for (iteration = 0; iteration < 8; ++iteration) locals[iteration] = UINT64_C(0xABCD0000) + index + iteration;
    for (iteration = 0; iteration < 100; ++iteration) {
        if (!x86_scheduler_task_valid() || progress[index] != iteration) kernel_scheduler_fail_stop();
        ++progress[index];
        x86_scheduler_yield();
        if (locals[iteration % 8] != UINT64_C(0xABCD0000) + index + iteration % 8) kernel_scheduler_fail_stop();
    }
}

// ====== x86_scheduler_workload ======

void x86_scheduler_workload(void) { workload_private(); event_workload_private(); }

// ====== x86_scheduler_workload_complete ======

int x86_scheduler_workload_complete(void) { return progress[0] == 100 && progress[1] == 100 && handshake == 5 && condition == 1; }
