#include "../test.h"
static uint32_t boot_cpu = 1;

// ====== ks_boot_cpu ======

uint32_t ks_boot_cpu(void) { return boot_cpu; }

// ====== test_wait_binding_errors ======

static void test_wait_binding_errors(KERNEL_LOCK_CONTEXT *context, const KERNEL_LOCK_WAIT *wait) {
    KERNEL_LOCK_WAIT invalid = *wait;
    KERNEL_LOCK_CONTEXT copy = *context;
    KERNEL_LOCK_CONTEXT fresh = {0};
    _Alignas(8) unsigned char storage[sizeof(KERNEL_LOCK_WAIT) + 8] = {0};
    assert(!kernel_lock_context_bind_wait(NULL, wait));
    assert(!kernel_lock_context_bind_wait(&copy, wait));
    assert(!kernel_lock_context_bind_wait(context, (const KERNEL_LOCK_WAIT *)context));
    assert(!kernel_lock_context_bind_wait(context, (const KERNEL_LOCK_WAIT *)(storage + 1)));
    invalid.Clock.Read = NULL;
    assert(!kernel_lock_context_bind_wait(context, &invalid));
    invalid = *wait;
    invalid.Ticks = 0;
    assert(!kernel_lock_context_bind_wait(context, &invalid));
    invalid.Ticks = wait->Clock.Mask / 2 + 1;
    assert(!kernel_lock_context_bind_wait(context, &invalid));
    invalid.Ticks = wait->Clock.Mask / 2;
    assert(!kernel_lock_context_bind_wait(context, &invalid));
    assert(!kernel_lock_context_initialize(&fresh, &invalid));
    --invalid.Ticks;
    assert(kernel_lock_context_initialize(&fresh, &invalid));
    assert(kernel_lock_context_bind_wait(context, &invalid));
    assert(kernel_lock_context_bind_wait(context, wait));
    invalid = *wait;
    invalid.MaxPolls = 0;
    assert(!kernel_lock_context_bind_wait(context, &invalid));
    assert(context->Wait == wait && ks_irq_state() == 0x200);
    test_set_if(0);
    assert(!kernel_lock_context_bind_wait(context, &invalid) && !ks_irq_state());
    assert(kernel_lock_context_bind_wait(context, NULL) && !ks_irq_state());
    assert(kernel_lock_context_bind_wait(context, wait) && !ks_irq_state());
    test_set_if(0x200);
}

// ====== test_provider ======

void test_provider(void) {
    KERNEL_LOCK lock = {0};
    KERNEL_LOCK_GUARD guard = {0};
    KERNEL_LOCK_WAIT wait = {{test_clock, NULL, UINT32_MAX, 1000}, 1000, 10000};
    test_set_if(0x200);
    assert(!kernel_lock_current_context() && ks_irq_state() == 0x200);
    boot_cpu = 0;
    assert(!kernel_lock_bootstrap_initialize() && !kernel_lock_current_context());
    assert(ks_irq_state() == 0x200);
    boot_cpu = 1;
    assert(kernel_lock_bootstrap_initialize() && !kernel_lock_bootstrap_initialize());
    KERNEL_LOCK_CONTEXT *context = kernel_lock_current_context();
    assert(context && context->Self == (uintptr_t)context && ks_irq_state() == 0x200);
    assert(kernel_lock_context_bind_wait(context, &wait) && context->Wait == &wait);
    test_wait_binding_errors(context, &wait);
    assert(kernel_lock_initialize(&lock, 1) && kernel_lock_acquire(&lock, context, &guard));
    assert(!kernel_lock_context_bind_wait(context, NULL) && !ks_irq_state());
    assert(kernel_lock_current_context() == context && !ks_irq_state());
    assert(kernel_lock_release(context, &guard) && ks_irq_state() == 0x200);
    assert(kernel_lock_context_bind_wait(context, NULL) && !context->Wait);
    boot_cpu = 0;
    assert(!kernel_lock_current_context() && !kernel_lock_bootstrap_initialize());
    boot_cpu = 1;
    assert(kernel_lock_current_context() == context && !context->Depth);
    puts("PASS: BSP context binding, AP/unprepared refusal, quiescent clock binding; CPU check mocked");
}

// ====== per_cpu_boundary_mock_for_early_provider_tests ======

int x86_percpu_bsp_promote(KERNEL_LOCK_CONTEXT *early) { (void)early; return 0; }
KERNEL_LOCK_CONTEXT *x86_percpu_current_context(void) { return NULL; }
