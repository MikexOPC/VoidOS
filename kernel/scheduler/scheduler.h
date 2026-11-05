#ifndef OS_KERNEL_SCHEDULER_H
#define OS_KERNEL_SCHEDULER_H

#include <stdint.h>
#include <stddef.h>

#define KERNEL_SCHEDULER_CAPACITY 8U
#define KERNEL_SCHEDULER_NONE UINT32_MAX

#define KERNEL_SCHEDULER_INIT 0U
#define KERNEL_SCHEDULER_RESERVE 1U
#define KERNEL_SCHEDULER_PUBLISH 2U
#define KERNEL_SCHEDULER_ABORT 3U
#define KERNEL_SCHEDULER_SELECT 4U
#define KERNEL_SCHEDULER_YIELD 5U
#define KERNEL_SCHEDULER_EXIT 6U
#define KERNEL_SCHEDULER_COLLECT 7U
#define KERNEL_SCHEDULER_QUERY 8U
#define KERNEL_SCHEDULER_CURRENT 9U
#define KERNEL_SCHEDULER_PREEMPT 10U

#define KERNEL_SCHEDULER_OK 0U
#define KERNEL_SCHEDULER_CORRUPT 1U
#define KERNEL_SCHEDULER_FULL 2U
#define KERNEL_SCHEDULER_BAD_HANDLE 3U
#define KERNEL_SCHEDULER_BAD_STATE 4U
#define KERNEL_SCHEDULER_RUNNING 5U
#define KERNEL_SCHEDULER_EMPTY 6U
#define KERNEL_SCHEDULER_RETIRED 7U
#define KERNEL_SCHEDULER_EXHAUSTED 8U
#define KERNEL_SCHEDULER_BUSY 9U
#define KERNEL_SCHEDULER_DENIED 10U
#define KERNEL_SCHEDULER_NOT_INITIALIZED 11U
#define KERNEL_SCHEDULER_BAD_ARGUMENT 12U
#define KERNEL_SCHEDULER_ALREADY_INITIALIZED 13U
#define KERNEL_SCHEDULER_BLOCKED 14U
#define KERNEL_SCHEDULER_BAD_EVENT 15U
#define KERNEL_SCHEDULER_BAD_TOKEN 16U
#define KERNEL_SCHEDULER_BAD_CPU 17U
#define KERNEL_EVENT_OBSERVE 0U
#define KERNEL_EVENT_WAIT 1U
#define KERNEL_EVENT_SIGNAL 2U
#define KERNEL_EVENT_WAITING 1U
#define KERNEL_EVENT_CHANGED 2U
#define KERNEL_EVENT_STOP_REQUESTED 5U
#define KERNEL_STOP_REQUEST 0U
#define KERNEL_STOP_QUERY 1U

#define KERNEL_TASK_FREE 0U
#define KERNEL_TASK_RESERVED 1U
#define KERNEL_TASK_READY 2U
#define KERNEL_TASK_RUNNING 3U
#define KERNEL_TASK_FINISHED 4U
#define KERNEL_TASK_RETIRED 5U
#define KERNEL_TASK_WAITING 6U

typedef struct {
    uint32_t  status;
    uint32_t  index;
    uint64_t  generation;
    uint32_t  state;
    uint32_t  ready;
    uint64_t  events;
} KERNEL_SCHEDULER_REPLY;

typedef struct {
    KERNEL_SCHEDULER_REPLY  reply;
    uint64_t                epoch;
    uint32_t                outcome;
    uint32_t                woken;
} KERNEL_EVENT_REPLY;
_Static_assert(sizeof(KERNEL_EVENT_REPLY) == 48, "Event reply ABI size");
_Static_assert(_Alignof(KERNEL_EVENT_REPLY) == 8, "Event reply ABI alignment");
_Static_assert(offsetof(KERNEL_EVENT_REPLY, reply) == 0, "Event nested reply ABI");
_Static_assert(offsetof(KERNEL_EVENT_REPLY, epoch) == 32, "Event epoch ABI");
_Static_assert(offsetof(KERNEL_EVENT_REPLY, outcome) == 40, "Event outcome ABI");
_Static_assert(offsetof(KERNEL_EVENT_REPLY, woken) == 44, "Event woken ABI");

_Static_assert(sizeof(KERNEL_SCHEDULER_REPLY) == 32, "Scheduler reply ABI size");
_Static_assert(_Alignof(KERNEL_SCHEDULER_REPLY) == 8, "Scheduler reply ABI alignment");
_Static_assert(offsetof(KERNEL_SCHEDULER_REPLY, status) == 0, "Reply status ABI");
_Static_assert(offsetof(KERNEL_SCHEDULER_REPLY, index) == 4, "Reply index ABI");
_Static_assert(offsetof(KERNEL_SCHEDULER_REPLY, generation) == 8, "Reply generation ABI");
_Static_assert(offsetof(KERNEL_SCHEDULER_REPLY, state) == 16, "Reply state ABI");
_Static_assert(offsetof(KERNEL_SCHEDULER_REPLY, ready) == 20, "Reply ready ABI");
_Static_assert(offsetof(KERNEL_SCHEDULER_REPLY, events) == 24, "Reply events ABI");

// ====== kernel_scheduler_apply ======

// Private dispatcher API. No pointers to Rust owner escape. INIT is one-shot.
// RESERVE/SELECT/CURRENT/INIT require index=NONE, generation=0.
// Other commands take a matching-generation handle. QUERY may report FREE/RETIRED.
// Error outputs have index=NONE/gen=0; BUSY/DENIED do not expose owner snapshots.
// PUBLISH follows native validation; ABORT/COLLECT follow verified cleanup.
// Caller and linked context predicate must enforce BSP/IF=0/DF=0, outside IRQ/NMI.
KERNEL_SCHEDULER_REPLY kernel_scheduler_apply(uint32_t operation, uint32_t index,
                                             uint64_t generation);

// ====== kernel_scheduler_event_apply ======

// Dispatcher only, matching RUNNING handle. OBSERVE/SIGNAL require observed=0.
// WAIT compares an observed epoch atomically with registration; future tokens fail.
// SIGNAL wakes all matching waiters, never frees their native resources.
KERNEL_EVENT_REPLY kernel_scheduler_event_apply(uint32_t operation, uint32_t index,
    uint64_t generation, uint32_t event, uint64_t observed);

// ====== irq_notification_boundary ======

// post: private bound ISR only, notifications coalesce, never borrows the owner.
// drain: verified dispatcher with IF=0 and no RUNNING task; rejects retain bits.
#if defined(SCHEDULER_HOST_UNBOUND)
uint32_t kernel_scheduler_irq_post(uint32_t event); // Regression fixtures only.
#endif
// Trusted backend identity, no pointer dereference in Rust. Zero means rejection.
// bind/unbind require BSP/IF0/DF0/main stack and no running task.
// unbind additionally requires hardware quiescence verified by the caller.
uint64_t kernel_scheduler_irq_bind(uint32_t event, uint64_t source);
uint32_t kernel_scheduler_irq_unbind(uint32_t event, uint64_t source, uint64_t generation);
uint32_t kernel_scheduler_irq_post_bound(uint32_t event, uint64_t source, uint64_t generation);
uint32_t kernel_scheduler_binding_context_valid(void);
KERNEL_EVENT_REPLY kernel_scheduler_irq_drain(void);

// ====== cooperative_stop_boundary ======

// Dispatcher only; REQUEST requires no RUNNING task, wakes WAITING without EXIT.
// QUERY returns outcome=0/1; REQUEST outcome=1 and woken=0/1. epoch is zero.
// Duplicates are no-ops. Handle generation binds the flag; COLLECT clears it.
KERNEL_EVENT_REPLY kernel_scheduler_stop_apply(uint32_t operation, uint32_t index,
    uint64_t generation);

// ====== smp_boundary ======

KERNEL_SCHEDULER_REPLY kernel_scheduler_apply_on(uint32_t cpu, uint32_t operation, uint32_t index,
    uint64_t generation);
KERNEL_EVENT_REPLY kernel_scheduler_event_apply_on(uint32_t cpu, uint32_t operation, uint32_t index,
    uint64_t generation, uint32_t event, uint64_t observed);
KERNEL_EVENT_REPLY kernel_scheduler_stop_apply_on(uint32_t cpu, uint32_t operation, uint32_t index,
    uint64_t generation);
uint32_t kernel_scheduler_configure(uint32_t cpus, uint32_t slice);
uint32_t kernel_scheduler_affinity_on(uint32_t cpu, uint32_t index, uint64_t generation, uint64_t mask);
KERNEL_SCHEDULER_REPLY kernel_scheduler_tick_on(uint32_t cpu);

// ====== scheduler_backend_requirements ======

// Backend symbols provided by x86 scheduler integration; host fixtures mock them.
uint32_t kernel_scheduler_context_valid(void);
uint32_t kernel_scheduler_context_valid_on(uint32_t cpu);
__declspec(noreturn) void kernel_scheduler_fail_stop(void);
uint32_t kernel_scheduler_lock_acquire(void);
void kernel_scheduler_lock_release(void);

#endif
