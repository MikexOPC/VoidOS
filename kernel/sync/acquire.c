#include "internal.h"

#define LOCK_CLOCK_POLL_INTERVAL 64U

typedef struct {
    uint32_t  Started, Polls;
    KERNEL_DEADLINE  Deadline;
} KERNEL_LOCK_PROGRESS;

// ====== wait_private ======

static int wait_private(const KERNEL_LOCK_CONTEXT *context, KERNEL_LOCK_PROGRESS *progress) {
    const KERNEL_LOCK_WAIT *wait = context->Wait;
    if (!wait) return 0;
    if (!progress->Started) {
        int started = kernel_deadline_start(&wait->Clock, wait->Ticks, &progress->Deadline);
        if (ks_irq_state()) ks_fail_stop("LOCK_CLOCK_IRQ_ENABLED");
        if (!started) return 0;
        progress->Started = 1;
    }
    if (progress->Polls >= wait->MaxPolls)
        return 0;
    if (!(progress->Polls % LOCK_CLOCK_POLL_INTERVAL)) {
        int expired = kernel_deadline_expired(&wait->Clock, &progress->Deadline);
        if (ks_irq_state())
            ks_fail_stop("LOCK_CLOCK_IRQ_ENABLED");
        if (expired)
            return 0;
    }
    ++progress->Polls;
    ks_pause();
    return 1;
}

// ====== preflight_private ======

static int preflight_private(KERNEL_LOCK *lock, KERNEL_LOCK_CONTEXT *context, KERNEL_LOCK_GUARD *guard) {
    if (!ks_context_valid(context) ||
        !ks_lock_valid(lock) ||
        !ks_span(guard, sizeof(*guard)) ||
        ks_overlap(lock, sizeof(*lock), context, sizeof(*context)) ||
        ks_overlap(guard, sizeof(*guard), lock, sizeof(*lock)) ||
        ks_overlap(guard, sizeof(*guard), context, sizeof(*context)) ||
        context->NextToken == UINT64_MAX
    )
        return 0;
    if (context->Wait && (!ks_span(context->Wait, sizeof(*context->Wait)) ||
        ks_overlap(guard, sizeof(*guard), context->Wait, sizeof(*context->Wait)) ||
        ks_overlap(lock, sizeof(*lock), context->Wait, sizeof(*context->Wait)))
    )
        return 0;
    uint32_t rank = context->Depth ? context->Frames[context->Depth - 1].Rank : 0;
    if (!ks_rank_enter(rank, lock->Rank, context->Depth)) return 0;
    for (uint32_t byte = 0; byte < sizeof(*guard); ++byte)
        if (((const unsigned char *)guard)[byte]) return 0;
    return 1;
}

// ====== acquire_private ======

static int acquire_private(KERNEL_LOCK *lock, KERNEL_LOCK_CONTEXT *context,
    KERNEL_LOCK_GUARD *guard, int attempt_only) {
    uint64_t saved_if = ks_irq_save();
    KERNEL_LOCK_PROGRESS progress = {0};
    if ((saved_if & ~0x200ULL) ||
        ks_irq_state()
    )
        ks_fail_stop("LOCK_IRQ_BACKEND_INVALID");
    if (ks_context_valid(context) && context->Depth && saved_if) ks_fail_stop("LOCK_NESTED_IRQ_ENABLED");
    if (!preflight_private(lock, context, guard)) {
        ks_irq_restore(saved_if);
        return 0;
    }
    uint64_t ticket;
    for (;;) {
        ticket = ks_atomic_load_relaxed(&lock->Next.Value);
        if (!ks_ticket_available(ticket)) {
            ks_irq_restore(saved_if);
            return 0;
        }
        if (attempt_only && ks_atomic_load_acquire(&lock->Serving.Value) != ticket) {
            ks_irq_restore(saved_if);
            return 0;
        }
        if (ks_atomic_compare(&lock->Next.Value, ticket, ticket + 1)) break;
        if (attempt_only ||
            !wait_private(context, &progress)
        ) {
            ks_irq_restore(saved_if);
            return 0;
        }
    }
    progress = (KERNEL_LOCK_PROGRESS){0};
    uint64_t observed = ks_atomic_load_acquire(&lock->Serving.Value);
    for (;;) {
        uint64_t serving = ks_atomic_load_acquire(&lock->Serving.Value);
        if (serving == ticket) break;
        if (serving > ticket) ks_fail_stop("LOCK_TICKET_PASSED");
        if (serving != observed) {
            observed = serving;
            progress = (KERNEL_LOCK_PROGRESS){0};
        }
        if (attempt_only ||
            !wait_private(context, &progress)
        ) {
            uint64_t latest = ks_atomic_load_acquire(&lock->Serving.Value);
            if (latest == ticket) break;
            if (!attempt_only && latest != serving)
                continue;
            ks_fail_stop("LOCK_WAIT_FAILED");
        }
    }
    KERNEL_LOCK_FRAME *frame = &context->Frames[context->Depth];
    frame->Lock = (uintptr_t)lock;
    frame->Guard = (uintptr_t)guard;
    frame->Ticket = ticket;
    frame->Token = context->NextToken++;
    frame->SavedIf = saved_if;
    frame->Rank = lock->Rank;
    frame->Reserved = 0;
    guard->Context = context;
    guard->Lock = lock;
    guard->Ticket = ticket;
    guard->Token = frame->Token;
    guard->SavedIf = saved_if;
    guard->Self = (uintptr_t)guard;
    ++context->Depth;
    return 1;
}

// ====== kernel_lock_acquire ======

int kernel_lock_acquire(KERNEL_LOCK *lock, KERNEL_LOCK_CONTEXT *context, KERNEL_LOCK_GUARD *guard) {
    return acquire_private(lock, context, guard, 0);
}

// ====== kernel_lock_try ======

int kernel_lock_try(KERNEL_LOCK *lock, KERNEL_LOCK_CONTEXT *context, KERNEL_LOCK_GUARD *guard) {
    return acquire_private(lock, context, guard, 1);
}
