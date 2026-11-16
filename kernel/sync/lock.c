#include "internal.h"

// ====== kernel_preempt_section_enter ======

uint64_t kernel_preempt_section_enter(void)
{
    return ks_irq_save();
}

// ====== kernel_preempt_section_leave ======

void kernel_preempt_section_leave(uint64_t saved_if)
{
    ks_irq_restore(saved_if);
}

// ====== ks_span ======

int ks_span(const void *object, size_t bytes) {
    return object && !((uintptr_t)object & 7) && (uintptr_t)object <= UINTPTR_MAX - bytes;
}

// ====== ks_overlap ======

int ks_overlap(const void *first, size_t first_bytes, const void *second, size_t second_bytes) {
    return (uintptr_t)first < (uintptr_t)second + second_bytes &&
        (uintptr_t)second < (uintptr_t)first + first_bytes;
}

// ====== ks_lock_valid ======

int ks_lock_valid(const KERNEL_LOCK *lock) {
    return ks_span(lock, sizeof(*lock)) && lock->Self == (uintptr_t)lock &&
        lock->Ready == 1 && lock->Rank && lock->Rank <= UINT16_MAX;
}

// ====== ks_context_valid ======

int ks_context_valid(const KERNEL_LOCK_CONTEXT *context) {
    if (!ks_span(context, sizeof(*context)) ||
        context->Self != (uintptr_t)context ||
        !context->NextToken ||
        context->Reserved ||
        context->Depth > KERNEL_LOCK_DEPTH
    )
        return 0;
    uint32_t previous = 0;
    for (uint32_t index = 0; index < context->Depth; ++index) {
        const KERNEL_LOCK_FRAME *frame = &context->Frames[index];
        if (!frame->Lock ||
            !frame->Guard ||
            !frame->Token ||
            frame->Token >= context->NextToken ||
            frame->Ticket == UINT64_MAX ||
            (frame->SavedIf & ~0x200ULL) ||
            frame->Reserved ||
            !ks_rank_enter(previous, frame->Rank, index)
        )
            return 0;
        previous = frame->Rank;
    }
    return 1;
}

// ====== kernel_lock_initialize ======

int kernel_lock_initialize(KERNEL_LOCK *lock, uint32_t rank) {
    if (!ks_span(lock, sizeof(*lock)) ||
        !ks_rank_enter(0, rank, 0) ||
        lock->Self ||
        lock->Ready ||
        lock->Rank ||
        lock->Next.Value ||
        lock->Serving.Value
    )
        return 0;
    ks_atomic_store_relaxed(&lock->Next.Value, 0);
    ks_atomic_store_relaxed(&lock->Serving.Value, 0);
    lock->Rank = rank;
    lock->Ready = 1;
    lock->Self = (uintptr_t)lock;
    return 1;
}

// ====== kernel_lock_context_initialize ======

int kernel_lock_context_initialize(KERNEL_LOCK_CONTEXT *context, const KERNEL_LOCK_WAIT *wait) {
    if (!ks_span(context, sizeof(*context)) ||
        context->Self ||
        context->NextToken ||
        context->Wait ||
        context->Depth ||
        context->Reserved
    )
        return 0;
    if (wait && (!ks_span(wait, sizeof(*wait)) ||
        ks_overlap(context, sizeof(*context), wait, sizeof(*wait)) ||
        !kernel_clock_valid(&wait->Clock) ||
        !wait->Ticks ||
        wait->Ticks >= wait->Clock.Mask / 2 ||
        !wait->MaxPolls)
    )
        return 0;
    for (uint32_t byte = 0; byte < sizeof(context->Frames); ++byte)
        if (((const unsigned char *)context->Frames)[byte]) return 0;
    context->Wait = wait;
    context->NextToken = 1;
    context->Self = (uintptr_t)context;
    return 1;
}

// ====== kernel_lock_context_bind_wait ======

int kernel_lock_context_bind_wait(KERNEL_LOCK_CONTEXT *context, const KERNEL_LOCK_WAIT *wait) {
    uint64_t saved_if = ks_irq_save();
    if (!ks_context_valid(context) ||
        context->Depth
    ) {
        ks_irq_restore(saved_if);
        return 0;
    }
    if (wait && (!ks_span(wait, sizeof(*wait)) ||
        ks_overlap(context, sizeof(*context), wait, sizeof(*wait)) ||
        !kernel_clock_valid(&wait->Clock) ||
        !wait->Ticks ||
        wait->Ticks >= wait->Clock.Mask / 2 ||
        !wait->MaxPolls)
    ) {
        ks_irq_restore(saved_if);
        return 0;
    }
    context->Wait = wait;
    ks_irq_restore(saved_if);
    return 1;
}

// ====== kernel_lock_release ======

int kernel_lock_release(KERNEL_LOCK_CONTEXT *context, KERNEL_LOCK_GUARD *guard) {
    uint64_t entry_if = ks_irq_save();
    if (!ks_context_valid(context) ||
        !context->Depth ||
        !ks_span(guard, sizeof(*guard)) ||
        ks_overlap(context, sizeof(*context), guard, sizeof(*guard)) ||
        guard->Self != (uintptr_t)guard ||
        guard->Context != context
    ) {
        ks_irq_restore(entry_if);
        return 0;
    }
    KERNEL_LOCK_FRAME *frame = &context->Frames[context->Depth - 1];
    if (frame->Guard != (uintptr_t)guard ||
        frame->Lock != (uintptr_t)guard->Lock ||
        frame->Ticket != guard->Ticket ||
        frame->Token != guard->Token ||
        frame->SavedIf != guard->SavedIf ||
        !ks_lock_valid(guard->Lock) ||
        frame->Rank != guard->Lock->Rank ||
        ks_overlap(guard->Lock, sizeof(*guard->Lock), guard, sizeof(*guard))
    ) {
        ks_irq_restore(entry_if);
        return 0;
    }
    if (entry_if) ks_fail_stop("LOCK_RELEASE_IRQ_ENABLED");
    if (ks_atomic_load_acquire(&guard->Lock->Serving.Value) != guard->Ticket)
        ks_fail_stop("LOCK_RELEASE_NOT_OWNER");
    if (ks_atomic_load_relaxed(&guard->Lock->Next.Value) <= guard->Ticket)
        ks_fail_stop("LOCK_NEXT_INVALID");
    KERNEL_LOCK *lock = guard->Lock;
    uint64_t next = guard->Ticket + 1, saved_if = guard->SavedIf;
    for (uint32_t byte = 0; byte < sizeof(*guard); ++byte) ((unsigned char *)guard)[byte] = 0;
    for (uint32_t byte = 0; byte < sizeof(*frame); ++byte) ((unsigned char *)frame)[byte] = 0;
    --context->Depth;
    ks_atomic_store_release(&lock->Serving.Value, next);
    ks_irq_restore(saved_if);
    return 1;
}
