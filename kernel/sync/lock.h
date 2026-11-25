#ifndef OS_KERNEL_SYNC_LOCK_H
#define OS_KERNEL_SYNC_LOCK_H
#include <stddef.h>
#include <stdint.h>
#include "kernel/time/clock.h"

// ====== synchronization_contract ======

#define KERNEL_LOCK_DEPTH  8U

typedef struct {
    uint64_t  Value;
} KERNEL_ATOMIC64;

typedef struct {
    uint64_t  Self;
    KERNEL_ATOMIC64  Next, Serving;
    uint32_t  Rank, Ready;
} KERNEL_LOCK;

typedef struct {
    KERNEL_CLOCK  Clock;
    uint32_t  Ticks, MaxPolls;
} KERNEL_LOCK_WAIT;

typedef struct {
    uint64_t  Lock, Guard, Ticket, Token, SavedIf;
    uint32_t  Rank, Reserved;
} KERNEL_LOCK_FRAME;

typedef struct {
    uint64_t  Self, NextToken;
    const KERNEL_LOCK_WAIT  *Wait;
    uint32_t  Depth, Reserved;
    KERNEL_LOCK_FRAME  Frames[KERNEL_LOCK_DEPTH];
} KERNEL_LOCK_CONTEXT;

typedef struct {
    uint64_t  Self;
    KERNEL_LOCK_CONTEXT  *Context;
    KERNEL_LOCK  *Lock;
    uint64_t  Ticket, Token, SavedIf;
} KERNEL_LOCK_GUARD;

_Static_assert(sizeof(KERNEL_ATOMIC64) == 8 && _Alignof(KERNEL_ATOMIC64) == 8, "Atomic64 ABI");
_Static_assert(sizeof(KERNEL_LOCK) == 32 && offsetof(KERNEL_LOCK, Next) == 8, "Ticket lock ABI");
_Static_assert(sizeof(KERNEL_LOCK_FRAME) == 48 && sizeof(KERNEL_LOCK_CONTEXT) == 416, "Per-CPU lock context ABI");
_Static_assert(sizeof(KERNEL_LOCK_GUARD) == 48 && sizeof(KERNEL_LOCK_WAIT) == 32, "Lock guard ABI");

uint64_t kernel_preempt_section_enter(void);
void kernel_preempt_section_leave(uint64_t saved_if);

int kernel_lock_initialize(KERNEL_LOCK *lock, uint32_t rank);
int kernel_lock_context_initialize(KERNEL_LOCK_CONTEXT *context, const KERNEL_LOCK_WAIT *wait);
int kernel_lock_context_bind_wait(KERNEL_LOCK_CONTEXT *context, const KERNEL_LOCK_WAIT *wait);
int kernel_lock_bootstrap_initialize(void);
int kernel_lock_bootstrap_promote(void);
KERNEL_LOCK_CONTEXT *kernel_lock_current_context(void);
typedef struct {
    uint64_t  Acquires;
    uint64_t  Contended;
    uint64_t  TotalWaitPolls;
    uint32_t  MaxWaitPolls;
    uint32_t  MaxWaitRank;
} KERNEL_LOCK_STATS;

int kernel_lock_acquire(KERNEL_LOCK *lock, KERNEL_LOCK_CONTEXT *context, KERNEL_LOCK_GUARD *guard);
int kernel_lock_try(KERNEL_LOCK *lock, KERNEL_LOCK_CONTEXT *context, KERNEL_LOCK_GUARD *guard);
int kernel_lock_release(KERNEL_LOCK_CONTEXT *context, KERNEL_LOCK_GUARD *guard);
void kernel_lock_stats_read(KERNEL_LOCK_STATS *out);
#endif
