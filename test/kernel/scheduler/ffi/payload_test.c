#include "kernel/scheduler/scheduler.h"
#include "kernel/scheduler/payload.h"
#include <stdio.h>
#include <stdlib.h>
static uint32_t checks, dispatcher = 1, running, task, nested;
static uint64_t generation = 7;

// ====== check_private ======

static void check_private(int condition)
{
    ++checks;
    if (!condition) { fprintf(stderr,"Payload check %u failed\n",checks); exit(1); }
}

// ====== trusted_context_mocks ======

uint32_t kernel_scheduler_context_valid(void) { return dispatcher; }
uint32_t kernel_scheduler_binding_context_valid(void) { return dispatcher; }
uint32_t kernel_scheduler_payload_context_valid(uint32_t consumer, uint64_t token)
{
    if (nested) {
        check_private(!kernel_scheduler_payload_push(0,1,1,99));
        check_private(kernel_scheduler_payload_pop(0,1,1,0,7) == KERNEL_PAYLOAD_BUSY);
    }
    return running && task == consumer && token == generation;
}
__declspec(noreturn) void kernel_scheduler_fail_stop(void) { exit(99); }

// ====== drain_private ======

static void drain_private(void)
{
    check_private(kernel_scheduler_irq_drain().reply.status == 0);
}

// ====== main ======

int main(void)
{
    check_private(kernel_scheduler_apply(0,KERNEL_SCHEDULER_NONE,0).status == 0);
    check_private(kernel_scheduler_irq_bind(0,1) == 1);
    check_private(!kernel_scheduler_payload_open(8,1,1,0));
    check_private(!kernel_scheduler_payload_open(0,2,1,0));
    check_private(!kernel_scheduler_payload_open(0,1,0,0));
    check_private(!kernel_scheduler_payload_open(0,1,1,8));
    dispatcher = 0; check_private(!kernel_scheduler_payload_open(0,1,1,0)); dispatcher = 1;
    check_private(kernel_scheduler_payload_open(0,1,1,0));
    check_private(!kernel_scheduler_payload_open(0,1,1,0));
    check_private(!kernel_scheduler_irq_unbind(0,1,1));
    check_private(!kernel_scheduler_irq_post_bound(0,1,1));
    check_private(!kernel_scheduler_payload_push(8,1,1,0));
    check_private(!kernel_scheduler_payload_push(0,2,1,0));
    check_private(!kernel_scheduler_payload_push(0,1,1,256));
    check_private(kernel_scheduler_payload_pop(0,1,1,0,7) == KERNEL_PAYLOAD_DENIED);
    running = 1; nested = 1;
    check_private(kernel_scheduler_payload_pop(0,1,1,0,7) == KERNEL_PAYLOAD_EMPTY);
    nested = 0;
    check_private(kernel_scheduler_payload_pop(8,1,1,0,7) == KERNEL_PAYLOAD_DENIED);
    check_private(kernel_scheduler_payload_pop(0,1,1,0,0) == KERNEL_PAYLOAD_DENIED);
    for (uint32_t round = 0; round < 1000; ++round) {
        for (uint32_t byte = 0; byte < 256; ++byte)
            check_private(kernel_scheduler_payload_push(0,1,1,byte) == 1);
        check_private(kernel_scheduler_payload_push(0,1,1,42) == 2);
        check_private(!kernel_scheduler_payload_close(0,1,1)); // Pending notification.
        drain_private();
        check_private(!kernel_scheduler_payload_close(0,1,1)); // Buffered data.
        task = 1; generation = 8;
        check_private(kernel_scheduler_payload_pop(0,1,1,1,8) == KERNEL_PAYLOAD_DENIED);
        task = 0;
        check_private(kernel_scheduler_payload_pop(0,1,1,0,8) == KERNEL_PAYLOAD_DENIED);
        generation = 7;
        check_private(kernel_scheduler_payload_pop(0,1,1,0,7) == KERNEL_PAYLOAD_DATA);
        check_private(kernel_scheduler_payload_push(0,1,1,99) == 2); // Free slot, loss barrier.
        for (uint32_t byte = 1; byte < 256; ++byte)
            check_private(kernel_scheduler_payload_pop(0,1,1,0,7) == (KERNEL_PAYLOAD_DATA | byte));
        drain_private();
        check_private(!kernel_scheduler_payload_close(0,1,1)); // Unobserved loss.
        check_private(kernel_scheduler_payload_pop(0,1,1,0,7) ==
            (KERNEL_PAYLOAD_LOSS | ((uint64_t)(2 * (round + 1)) << 32)));
        check_private(kernel_scheduler_payload_pop(0,1,1,0,7) == KERNEL_PAYLOAD_EMPTY);
    }
    check_private(kernel_scheduler_payload_close(0,1,1));
    check_private(!kernel_scheduler_payload_push(0,1,1,0));
    check_private(kernel_scheduler_irq_unbind(0,1,1));
    check_private(kernel_scheduler_irq_bind(0,1) == 2);
    check_private(!kernel_scheduler_payload_open(0,1,1,0));
    check_private(kernel_scheduler_payload_open(0,1,2,1));
    check_private(kernel_scheduler_payload_push(0,1,2,255) == 1);
    task = 1; generation = 9;
    check_private(kernel_scheduler_payload_pop(0,1,2,1,9) == (KERNEL_PAYLOAD_DATA | 255));
    drain_private();
    check_private(kernel_scheduler_payload_close(0,1,2));
    check_private(kernel_scheduler_irq_unbind(0,1,2));
    // Independent channels, one pending mask drain, no cross-channel consumption.
    for (uint32_t event = 0; event < 8; ++event) {
        uint64_t token = event == 0 ? 3 : 1;
        check_private(kernel_scheduler_irq_bind(event,10 + event) == token);
        check_private(kernel_scheduler_payload_open(event,10 + event,token,event));
        check_private(kernel_scheduler_payload_push(event,10 + event,token,event) == 1);
    }
    dispatcher = 0;
    check_private(!kernel_scheduler_payload_close(0,10,3));
    dispatcher = 1; drain_private();
    for (uint32_t event = 0; event < 8; ++event) {
        uint64_t token = event == 0 ? 3 : 1;
        task = event; generation = 100 + event;
        check_private(kernel_scheduler_payload_pop(event,10 + event,token,event,generation) ==
            (KERNEL_PAYLOAD_DATA | event));
        check_private(kernel_scheduler_payload_close(event,10 + event,token));
        check_private(kernel_scheduler_irq_unbind(event,10 + event,token));
    }
    printf("PASS: %u payload FFI checks, real Rust queue, CPU/hardware MOCK\n",checks);
    return 0;
}
