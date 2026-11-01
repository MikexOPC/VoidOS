#ifndef OS_KERNEL_SYNC_INTERNAL_H
#define OS_KERNEL_SYNC_INTERNAL_H
#include "lock.h"

// ====== synchronization_backend ======

uint64_t ks_atomic_load_relaxed(const uint64_t *value);
uint64_t ks_atomic_load_acquire(const uint64_t *value);
void ks_atomic_store_relaxed(uint64_t *value, uint64_t next);
void ks_atomic_store_release(uint64_t *value, uint64_t next);
uint32_t ks_atomic_compare(uint64_t *value, uint64_t expected, uint64_t next);
uint32_t ks_rank_enter(uint32_t held, uint32_t next, uint32_t depth);
uint32_t ks_ticket_available(uint64_t next);

uint64_t ks_irq_save(void);
uint64_t ks_irq_state(void);
void ks_irq_restore(uint64_t saved_if);
void ks_pause(void);
__declspec(noreturn) void ks_fail_stop(const char *reason);

int ks_span(const void *object, size_t bytes);
int ks_overlap(const void *first, size_t first_bytes, const void *second, size_t second_bytes);
int ks_context_valid(const KERNEL_LOCK_CONTEXT *context);
int ks_lock_valid(const KERNEL_LOCK *lock);
#endif
