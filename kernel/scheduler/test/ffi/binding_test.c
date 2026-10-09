#include "kernel/scheduler/scheduler.h"
#include <stdio.h>
#include <stdlib.h>
static uint32_t checks, context = 1, reenter;

// ====== check_private ======

static void check_private(int condition)
{
    ++checks;
    if (!condition) { fprintf(stderr,"Binding check %u failed\n",checks); exit(1); }
}

// ====== trusted_context_mocks ======

uint32_t kernel_scheduler_context_valid(void) { return context; }
// ====== payload_context_mock ======

uint32_t kernel_scheduler_payload_context_valid(uint32_t consumer, uint64_t generation)
{ (void)consumer; (void)generation; return context; }
// ====== binding_context_mock ======

uint32_t kernel_scheduler_binding_context_valid(void)
{
    if (reenter) {
        check_private(kernel_scheduler_irq_bind(7,9) == 0);
        check_private(kernel_scheduler_irq_unbind(0,1,1) == 0);
        check_private(kernel_scheduler_irq_post_bound(0,1,1) == 0);
    }
    return context;
}
__declspec(noreturn) void kernel_scheduler_fail_stop(void) { exit(99); }

// ====== main ======

int main(void)
{
    check_private(kernel_scheduler_apply(0,KERNEL_SCHEDULER_NONE,0).status == 0);
    check_private(kernel_scheduler_irq_bind(8,1) == 0);
    check_private(kernel_scheduler_irq_bind(0,0) == 0);
    context = 0; check_private(kernel_scheduler_irq_bind(0,1) == 0); context = 1;
    reenter = 1; check_private(kernel_scheduler_irq_bind(0,1) == 1); reenter = 0;
    check_private(kernel_scheduler_irq_bind(0,2) == 0);
    check_private(kernel_scheduler_irq_bind(1,1) == 0);
    check_private(kernel_scheduler_irq_unbind(0,2,1) == 0);
    check_private(kernel_scheduler_irq_post_bound(0,1,0) == 0);
    check_private(kernel_scheduler_irq_post_bound(0,2,1) == 0);
    check_private(kernel_scheduler_irq_unbind(0,1,1) == 1);
    for (uint64_t token = 2; token <= 10001; ++token) {
        check_private(kernel_scheduler_irq_bind(0,1) == token);
        check_private(!kernel_scheduler_irq_post_bound(0,1,token-1));
        check_private(!kernel_scheduler_irq_unbind(0,1,token-1));
        check_private(kernel_scheduler_irq_post_bound(0,1,token));
        check_private(kernel_scheduler_irq_post_bound(0,1,token));
        check_private(!kernel_scheduler_irq_unbind(0,1,token)); // Pending retained.
        context = 0;
        check_private(kernel_scheduler_irq_drain().reply.status == KERNEL_SCHEDULER_DENIED);
        check_private(!kernel_scheduler_irq_unbind(0,1,token));
        context = 1;
        check_private(kernel_scheduler_irq_drain().reply.status == 0);
        check_private(kernel_scheduler_irq_unbind(0,1,token));
        check_private(!kernel_scheduler_irq_post_bound(0,1,token));
    }
    check_private(kernel_scheduler_irq_bind(1,1) == 1); // Independent event counter.
    check_private(kernel_scheduler_irq_unbind(1,1,1));
    printf("PASS: %u binding FFI checks, real Rust registry, CPU/hardware MOCK\n",checks);
    return 0;
}
