#include "probe.h"
#include "kernel/sync/internal.h"
#include "kernel/diagnostics/log.h"
typedef struct {
    KERNEL_LOCK_CONTEXT  *Owner;
    KERNEL_LOCK_GUARD  *Guard;
    uint32_t  Reads;
} SYNC_HANDOFF;

// ====== require_private ======

static void require_private(int condition) {
    if (!condition) kernel_panic("KERNEL_SYNC_PROBE_FAILED");
}

// ====== handoff_private ======

static int handoff_private(void *state, uint32_t *value) {
    SYNC_HANDOFF *transfer = state;
    *value = ++transfer->Reads;
    if (transfer->Reads == 4) require_private(kernel_lock_release(transfer->Owner, transfer->Guard));
    return 1;
}

// ====== kernel_sync_probe ======

void kernel_sync_probe(void) {
    KERNEL_LOCK low = {0}, high = {0}, contended = {0};
    KERNEL_LOCK_CONTEXT context = {0}, other = {0};
    KERNEL_LOCK_GUARD outer = {0}, inner = {0}, unused = {0};
    require_private(!ks_irq_state());
    require_private(!kernel_lock_bootstrap_initialize());
    KERNEL_LOCK_CONTEXT *bound = kernel_lock_current_context();
    const KERNEL_LOCK_WAIT *runtime_wait = bound ? bound->Wait : NULL;
    require_private(bound && bound->Self == (uintptr_t)bound && !bound->Depth && !ks_irq_state());
    require_private(kernel_lock_initialize(&low, 10) && kernel_lock_initialize(&high, 20));
    require_private(kernel_lock_context_initialize(&context, NULL));
    require_private(kernel_lock_context_initialize(&other, NULL));
    for (uint32_t enabled = 0; enabled <= 1; ++enabled) {
        ks_irq_restore(enabled ? 0x200 : 0);
        require_private(kernel_lock_acquire(&low, bound, &outer) && !ks_irq_state());
        require_private(kernel_lock_current_context() == bound && !ks_irq_state());
        require_private(!kernel_lock_context_bind_wait(bound, NULL));
        require_private(!kernel_lock_try(&low, &other, &unused));
        require_private(!kernel_lock_try(&low, bound, &unused));
        require_private(kernel_lock_acquire(&high, bound, &inner) && !ks_irq_state());
        require_private(!kernel_lock_release(bound, &outer));
        KERNEL_LOCK_GUARD copy = inner;
        require_private(!kernel_lock_release(bound, &copy));
        require_private(kernel_lock_release(bound, &inner) && !ks_irq_state());
        require_private(kernel_lock_release(bound, &outer) && ks_irq_state() == (enabled ? 0x200ULL : 0));
        require_private(!kernel_lock_release(bound, &outer));
        require_private(!bound->Depth && !other.Depth);
        require_private(kernel_lock_context_bind_wait(bound, NULL));
        require_private(ks_irq_state() == (enabled ? 0x200ULL : 0));
        require_private(kernel_log(0, enabled ? "KERNEL_SYNC_IF1_RESTORED" : "KERNEL_SYNC_IF0_RESTORED"));
    }
    require_private(kernel_lock_context_bind_wait(bound, runtime_wait));
    require_private(kernel_log(0, "KERNEL_SYNC_BSP_CONTEXT_OK"));
    ks_irq_restore(0);
    KERNEL_LOCK_CONTEXT owner = {0}, waiter = {0};
    KERNEL_LOCK_GUARD held = {0}, waiting = {0};
    SYNC_HANDOFF transfer = {&owner, &held, 0};
    KERNEL_LOCK_WAIT wait = {{handoff_private, &transfer, UINT32_MAX, 1000}, 100, 100};
    require_private(kernel_lock_initialize(&contended, 1));
    require_private(kernel_lock_context_initialize(&owner, NULL));
    require_private(kernel_lock_context_initialize(&waiter, &wait));
    require_private(kernel_lock_acquire(&contended, &owner, &held));
    require_private(kernel_lock_acquire(&contended, &waiter, &waiting) && waiting.Ticket == 1);
    require_private(transfer.Reads == 4 && !owner.Depth && !ks_irq_state());
    require_private(kernel_lock_release(&waiter, &waiting));
    require_private(!waiter.Depth && !ks_irq_state());
    require_private(ks_atomic_load_acquire(&contended.Serving.Value) == 2);
    require_private(ks_atomic_load_relaxed(&contended.Next.Value) == 2);
    require_private(kernel_log(0, "KERNEL_SYNC_HANDOFF_SIMULATED"));
    require_private(kernel_log(0, "KERNEL_SYNC_GUARDS_REJECTED"));
#ifdef KERNEL_TEST_SYNC_FAULT
    require_private(kernel_lock_acquire(&low, &context, &outer));
    require_private(kernel_log(1, "KERNEL_SYNC_EXPECTED_FAILURE_ARMED"));
#if KERNEL_TEST_SYNC_FAULT == 1
    require_private(runtime_wait && kernel_lock_context_bind_wait(&other, runtime_wait));
    require_private(kernel_log(1, "KERNEL_PERCPU_WAIT_TIMEOUT_ARMED"));
    (void)kernel_lock_acquire(&low, &other, &unused);
#else
    ks_irq_restore(0x200);
    (void)kernel_lock_release(&context, &outer);
#endif
    kernel_panic("KERNEL_SYNC_EXPECTED_FAILURE_MISSING");
#else
    require_private(kernel_log(0, "KERNEL_SYNC_PROBE_OK"));
#endif
}
