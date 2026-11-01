#include "../platform.h"
#include "arch/x86/kernel/cpu/percpu/clock/clock.h"
#include "arch/x86/kernel/cpu/percpu/internal.h"
#include "runtime.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint32_t counter, step = 1, reads;

// ====== x86_pm_read_model ======

BOOT_U32 x86_pm_read(unsigned short port) {
    assert(port == 0x408 && !model_if);
    ++reads; counter += step; return counter;
}

// ====== valid_source_private ======

static X86_PM_CLOCK valid_source_private(uint32_t mask) {
    X86_PM_CLOCK source = {0x100000, 220, 0x408, mask, 1};
    return source;
}

// ====== invalid_inputs_private ======

static void invalid_inputs_private(X86_PM_CLOCK *source, KERNEL_LOCK_CONTEXT *context) {
    assert(!x86_percpu_wait_current() && !x86_percpu_wait_bind(NULL, 100, 20));
    assert(!x86_percpu_wait_bind(source, 0, 20));
    assert(!x86_percpu_wait_bind(source, source->Mask / 2, 20));
    assert(!x86_percpu_wait_bind(source, UINT32_MAX, 20));
    assert(!x86_percpu_wait_bind(source, 100, 0));
    X86_PM_CLOCK invalid = *source;
    invalid.Ready = 2; assert(!x86_percpu_wait_bind(&invalid, 100, 20));
    invalid = *source; invalid.Mask = 0xffff; assert(!x86_percpu_wait_bind(&invalid, 100, 20));
    invalid = *source; invalid.Port = 0x3f8; assert(!x86_percpu_wait_bind(&invalid, 100, 20));
    invalid = *source; invalid.Port = 0x10000; assert(!x86_percpu_wait_bind(&invalid, 100, 20));
    invalid = *source; invalid.Port = 0x409; assert(!x86_percpu_wait_bind(&invalid, 100, 20));
    invalid = *source; invalid.Physical = UINT64_MAX; assert(!x86_percpu_wait_bind(&invalid, 100, 20));
    invalid = *source; invalid.Length = 4097; assert(!x86_percpu_wait_bind(&invalid, 100, 20));
    assert(!x86_percpu_wait_bind((const X86_PM_CLOCK *)x86_percpu_bsp(), 100, 20));
    KERNEL_LOCK lock = {0}; KERNEL_LOCK_GUARD guard = {0};
    assert(kernel_lock_initialize(&lock, 100) && kernel_lock_acquire(&lock, context, &guard));
    assert(!x86_percpu_wait_bind(source, 100, 20) && !model_if && context->Depth == 1);
    assert(kernel_lock_release(context, &guard));
    model_bsp = 0; assert(!x86_percpu_wait_bind(source, 100, 20)); model_bsp = 1;
    assert(!reads && !context->Wait);
    step = 0; assert(!x86_percpu_wait_bind(source, 100, 4));
    assert(reads == 5 && !context->Wait && !x86_percpu_wait_current());
    step = source->Mask / 2; assert(!x86_percpu_wait_bind(source, 100, 4));
    assert(!context->Wait); step = 1;
}

// ====== terminal_private ======

static void terminal_private(const char *mode, KERNEL_LOCK_CONTEXT *context) {
    KERNEL_LOCK lock = {0}; KERNEL_LOCK_GUARD guard = {0};
    assert(kernel_lock_initialize(&lock, 100));
    ks_atomic_store_relaxed(&lock.Next.Value, 1);
    counter = 0; step = strcmp(mode, "stalled") ? 1 : 0;
    reads = 0; model_expect_fatal = 1;
    if (!setjmp(model_terminal)) {
        (void)kernel_lock_acquire(&lock, context, &guard);
        assert(!"Reserved ticket incorrectly continued");
    }
    assert(!strcmp(model_reason, "LOCK_WAIT_FAILED") && !model_if && !context->Depth);
    assert(lock.Next.Value == 2 && !lock.Serving.Value && !guard.Self && reads <= 12);
    puts("PERCPU_WAIT_EXPECTED_STOP: reserved ticket retained; deadline or stalled poll bound; host-only injection");
}

// ====== main ======

int main(int argc, char **argv) {
    assert(argc == 2);
    uint32_t mask = !strcmp(argv[1], "32") ? UINT32_MAX : 0xffffffU;
    X86_PM_CLOCK source = valid_source_private(mask);
    assert(!x86_percpu_wait_bind(&source, 4, 8));
    assert(kernel_lock_bootstrap_initialize() && kernel_lock_bootstrap_promote());
    KERNEL_LOCK_CONTEXT *context = kernel_lock_current_context();
    assert(context && !context->Wait && model_if == 0x200);
    invalid_inputs_private(&source, context);
    uint64_t expected_if = !strcmp(argv[1], "if0") ? 0 : 0x200;
    model_if = expected_if; counter = mask - 1;
    uint64_t tokens = context->NextToken;
    assert(x86_percpu_wait_bind(&source, 4, 8) && model_if == expected_if);
    const KERNEL_LOCK_WAIT *wait = x86_percpu_wait_current();
    assert(wait && wait == context->Wait && wait->Clock.Mask == mask && wait->Clock.Hz == 3579545);
    assert(wait->Ticks == 4 && wait->MaxPolls == 8 && context->NextToken == tokens);
    assert(wait->Clock.Context != &source && !x86_percpu_wait_bind(&source, 4, 8));
    assert(!x86_percpu_wait_bind(wait->Clock.Context, 4, 8));
    memset(&source, 0, sizeof(source));
    uint64_t saved = ks_irq_save(); uint32_t value;
    assert(kernel_clock_now(&wait->Clock, &value) && value == 1 && !model_if);
    ks_irq_restore(saved);
    assert(x86_percpu_wait_current() == wait && model_if == expected_if);
    X86_PM_CLOCK *owned_source = wait->Clock.Context;
    owned_source->Ready = 0;
    assert(!x86_percpu_wait_current() && !kernel_clock_now(&wait->Clock, &value));
    owned_source->Ready = 1;
    KERNEL_LOCK_WAIT *owned_wait = (KERNEL_LOCK_WAIT *)wait;
    owned_wait->MaxPolls = 0; assert(!x86_percpu_wait_current()); owned_wait->MaxPolls = 8;
    assert(x86_percpu_wait_current() == wait);
    model_bsp = 0; assert(!x86_percpu_wait_current()); model_bsp = 1;
    if (!strcmp(argv[1], "timeout") || !strcmp(argv[1], "stalled")) {
        terminal_private(argv[1], context); return 0;
    }
    model_if = 0x200;
    assert(!x86_percpu_wait_ap_prepare(1) && !x86_percpu_wait_ap_cancel(1) && !x86_percpu_ap_seal(4));
    model_if = expected_if;
    saved = ks_irq_save();
    assert(!x86_percpu_ap_seal(1) && !x86_percpu_ap_seal(65));
    model_bsp = 0; assert(!x86_percpu_wait_ap_prepare(1)); model_bsp = 1;
    assert(!x86_percpu_wait_ap_prepare(0) && !x86_percpu_wait_ap_prepare(64));
    for (uint32_t index = 1; index < 64; ++index) {
        assert(x86_percpu_wait_ap_prepare(index) && x86_percpu_wait_ap_valid(index));
        const X86_CPU_LOCAL *ap = x86_percpu_ap(index);
        assert(ap && ap->Index == index && ap->Locks.NextToken == 1 && !ap->EarlyContext);
        assert(ap->Locks.Wait != wait && ap->Locks.Wait->Clock.Context != wait->Clock.Context);
        assert(!x86_percpu_wait_ap_prepare(index));
        X86_CPU_LOCAL *mutable = (X86_CPU_LOCAL *)ap;
        mutable->Locks.NextToken = 2;
        assert(!x86_percpu_wait_ap_valid(index) && !x86_percpu_wait_ap_cancel(index));
        mutable->Locks.NextToken = 1;
        assert(x86_percpu_wait_ap_valid(index));
    }
    assert(!x86_percpu_ap_seal(4));
    for (uint32_t index = 63; index > 0; --index) assert(x86_percpu_wait_ap_cancel(index));
    step = 0; assert(!x86_percpu_wait_ap_prepare(1) && !x86_percpu_ap(1)); step = 1;
    assert(x86_percpu_wait_ap_prepare(1));
    step = 0; assert(!x86_percpu_wait_ap_prepare(2) && !x86_percpu_ap(2)); step = 1;
    assert(x86_percpu_wait_ap_cancel(1));
    uint32_t ap_count = !strcmp(argv[1], "ap2") ? 2 : !strcmp(argv[1], "ap64") || !strcmp(argv[1], "runtime64") ? 64 : 4;
    for (uint32_t index = 1; index < ap_count; ++index) assert(x86_percpu_wait_ap_prepare(index));
    assert(x86_percpu_ap_seal(ap_count) && !x86_percpu_ap_seal(ap_count));
    for (uint32_t index = 1; index < ap_count; ++index) {
        assert(!x86_percpu_wait_ap_cancel(index) && x86_percpu_wait_ap_valid(index));
    }
    assert(!x86_percpu_wait_ap_prepare(ap_count));
    assert(context->NextToken == tokens && context->Wait == wait);
    if (!strncmp(argv[1], "runtime", 7)) {
        runtime_private(ap_count, !strcmp(argv[1], "runtime-reject") ? 2 : strcmp(argv[1], "runtime-staged") != 0,
            !strcmp(argv[1], "runtime-sparse"));
        ks_irq_restore(saved);
        return 0;
    }
    ks_irq_restore(saved);
    KERNEL_LOCK lock = {0}; KERNEL_LOCK_GUARD guard = {0};
    assert(kernel_lock_initialize(&lock, 100) && kernel_lock_acquire(&lock, context, &guard));
    assert(context->Wait == wait && !model_if && kernel_lock_release(context, &guard));
    assert(model_if == expected_if && !context->Depth && x86_percpu_wait_current() == wait);
    assert(kernel_lock_context_bind_wait(context, NULL) && !x86_percpu_wait_current());
    source = valid_source_private(mask);
    assert(!x86_percpu_wait_bind(&source, 4, 8));
    assert(kernel_lock_context_bind_wait(context, wait) && x86_percpu_wait_current() == wait);
    puts("PERCPU_WAIT_PASS: actual provider and lock, owned source copy, bounded probe, wrap, IF and refusal paths; PM I/O modeled");
    return 0;
}
