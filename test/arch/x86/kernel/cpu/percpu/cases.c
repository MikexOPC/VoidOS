#include "platform.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

// ====== clock_private ======

static int clock_private(void *context, uint32_t *value) {
    (void)context; *value = 1; return 1;
}
static const KERNEL_LOCK_WAIT wait = {{clock_private, NULL, UINT32_MAX, 1000}, 1000, 10000};

// ====== preflight_private ======

static KERNEL_LOCK_CONTEXT *preflight_private(void) {
    assert(!kernel_lock_current_context() && !kernel_lock_bootstrap_promote());
    model_bsp = 0;
    assert(!kernel_lock_bootstrap_initialize() && !kernel_lock_current_context());
    assert(!model_gs_reads && !model_writes && model_if == 0x200);
    model_bsp = 1;
    assert(kernel_lock_bootstrap_initialize() && !kernel_lock_bootstrap_initialize());
    KERNEL_LOCK_CONTEXT *early = kernel_lock_current_context();
    assert(early && !early->Depth && early->Self == (uintptr_t)early);
    assert(!x86_percpu_bsp_promote(NULL));
    KERNEL_LOCK_CONTEXT foreign = {0};
    assert(kernel_lock_context_initialize(&foreign, NULL) && !x86_percpu_bsp_promote(&foreign));
    model_supported = 0;
    assert(!kernel_lock_bootstrap_promote()); model_supported = 1;
    model_image = 0;
    assert(!kernel_lock_bootstrap_promote()); model_image = 1;
    model_descriptors = 0;
    assert(!kernel_lock_bootstrap_promote()); model_descriptors = 1;
    KERNEL_LOCK lock = {0}; KERNEL_LOCK_GUARD guard = {0};
    assert(kernel_lock_initialize(&lock, 100) && kernel_lock_acquire(&lock, early, &guard));
    assert(!kernel_lock_bootstrap_promote() && !model_if && early->Depth == 1);
    assert(kernel_lock_release(early, &guard) && model_if == 0x200);
    KERNEL_LOCK_WAIT alias = wait;
    alias.Clock.Context = early;
    assert(kernel_lock_context_bind_wait(early, &alias));
    assert(!kernel_lock_bootstrap_promote() && kernel_lock_current_context() == early);
    assert(kernel_lock_context_bind_wait(early, &wait));
    assert(!model_writes && !model_self_reads && kernel_lock_current_context() == early);
    return early;
}

// ====== successful_private ======

static void successful_private(int disabled) {
    KERNEL_LOCK_CONTEXT *early = preflight_private();
    uint64_t next = early->NextToken;
    model_if = disabled ? 0 : 0x200;
    assert(kernel_lock_bootstrap_promote() && model_if == (disabled ? 0U : 0x200U));
    const X86_CPU_LOCAL *owner = x86_percpu_bsp();
    assert(owner && model_gs == (uintptr_t)owner && !owner->Index && owner->State == X86_PERCPU_ACTIVE);
    KERNEL_LOCK_CONTEXT *current = kernel_lock_current_context();
    assert(current == &owner->Locks && current != early && current->NextToken == next && current->Wait == &wait);
    for (size_t byte = 0; byte < sizeof(*early); ++byte) assert(!((const unsigned char *)early)[byte]);
    assert(!kernel_lock_bootstrap_initialize() && !kernel_lock_bootstrap_promote());
    KERNEL_LOCK lock = {0}; KERNEL_LOCK_GUARD guard = {0};
    assert(kernel_lock_initialize(&lock, 200));
    assert(!kernel_lock_acquire(&lock, early, &guard));
    assert(kernel_lock_acquire(&lock, current, &guard) && !model_if);
    assert(kernel_lock_release(current, &guard) && model_if == (disabled ? 0U : 0x200U));
    uint64_t base = model_gs;
    uint32_t reads = model_self_reads;
    model_gs = 0;
    assert(!kernel_lock_current_context() && model_self_reads == reads);
    model_gs = 8;
    assert(!kernel_lock_current_context() && model_self_reads == reads);
    model_gs = base;
    model_self_fault = 1;
    assert(!kernel_lock_current_context()); model_self_fault = 0;
    X86_CPU_LOCAL *mutable = (X86_CPU_LOCAL *)owner;
    mutable->Reserved = 1; assert(!kernel_lock_current_context()); mutable->Reserved = 0;
    mutable->Index = X86_PERCPU_CAPACITY; assert(!kernel_lock_current_context()); mutable->Index = 0;
    mutable->State = X86_PERCPU_PREPARED; assert(!kernel_lock_current_context()); mutable->State = X86_PERCPU_ACTIVE;
    uintptr_t self = current->Self;
    current->Self = 0; assert(!kernel_lock_current_context()); current->Self = self;
    model_bsp = 0; reads = model_gs_reads;
    assert(!kernel_lock_current_context() && !kernel_lock_bootstrap_initialize() && !kernel_lock_bootstrap_promote());
    assert(!x86_percpu_bsp() && model_gs_reads == reads);
    model_bsp = 1;
    assert(kernel_lock_current_context() == current && !current->Depth);
    assert(kernel_lock_context_bind_wait(current, NULL));
    assert(model_writes == 1 && model_if == (disabled ? 0U : 0x200U));
    puts("PERCPU_PASS: actual provider, early retirement, token/clock transfer, invalid GS/owner refusal, IF0/IF1 and BSP-only gating; MSR model");
}

// ====== failed_write_private ======

static void failed_write_private(int bad_self) {
    KERNEL_LOCK_CONTEXT *early = preflight_private();
    model_write_fault = !bad_self; model_self_fault = (uint32_t)bad_self;
    model_expect_fatal = 1;
    if (!setjmp(model_terminal)) { (void)kernel_lock_bootstrap_promote(); assert(0); }
    assert(model_reason && !strcmp(model_reason, "PERCPU_GS_VERIFY_FAILED") && !model_if);
    const X86_CPU_LOCAL *retained = (const X86_CPU_LOCAL *)(uintptr_t)model_target;
    assert(retained && retained->State == X86_PERCPU_PREPARED && ks_context_valid(&retained->Locks));
    assert(ks_context_valid(early) && retained->EarlyContext == (uintptr_t)early && model_writes == 1);
    assert(!kernel_lock_bootstrap_promote() && model_writes == 1);
    puts("PERCPU_EXPECTED_STOP: write/readback or GS self mismatch; prepared owner retained, no retry or partial activation; longjmp is host-only");
}

// ====== main ======

int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "exhausted")) {
        KERNEL_LOCK_CONTEXT *early = preflight_private();
        early->NextToken = UINT64_MAX;
        assert(kernel_lock_bootstrap_promote());
        KERNEL_LOCK_CONTEXT *current = kernel_lock_current_context();
        KERNEL_LOCK lock = {0}; KERNEL_LOCK_GUARD guard = {0};
        assert(current && current->NextToken == UINT64_MAX && kernel_lock_initialize(&lock, 100));
        assert(!kernel_lock_acquire(&lock, current, &guard) && model_if == 0x200);
        puts("PERCPU_TOKEN_LIMIT_PASS: promotion does not reset an exhausted counter");
    }
    else if (argc == 2 && !strcmp(argv[1], "bad-base")) failed_write_private(0);
    else if (argc == 2 && !strcmp(argv[1], "bad-self")) failed_write_private(1);
    else successful_private(argc == 2 && !strcmp(argv[1], "if0"));
    return 0;
}
