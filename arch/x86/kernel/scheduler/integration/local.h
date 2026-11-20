#ifndef X86_SCHEDULER_LOCAL_H
#define X86_SCHEDULER_LOCAL_H
#include "../context.h"
#include "../dispatcher/engine.h"
#include "arch/x86/kernel/cpu/percpu/percpu.h"

typedef __declspec(align(64)) struct {
    X86_TASK_CONTEXT     dispatcher;
    X86_DISPATCH_STATS   stats;
    uint64_t            generation, requested_epoch;
    uint32_t            phase, current, event, requested_event;
    uint64_t            self, owner;
    uint64_t            idle_cycles;
    uint64_t            runtime_ready, reserved[2];
} X86_SCHEDULER_LOCAL;
_Static_assert(_Alignof(X86_SCHEDULER_LOCAL) == 64, "Dispatcher cache alignment");
_Static_assert(offsetof(X86_SCHEDULER_LOCAL, dispatcher) == 0, "Native context offset");
_Static_assert(sizeof(X86_SCHEDULER_LOCAL) % 64 == 0, "No shared dispatcher cache line");
_Static_assert(sizeof(X86_SCHEDULER_LOCAL) == 704, "Dispatcher storage budget");
_Static_assert(offsetof(X86_SCHEDULER_LOCAL, stats) == 592 &&
    offsetof(X86_SCHEDULER_LOCAL, phase) == 640 &&
    offsetof(X86_SCHEDULER_LOCAL, self) == 656 &&
    offsetof(X86_SCHEDULER_LOCAL, idle_cycles) == 672 &&
    offsetof(X86_SCHEDULER_LOCAL, runtime_ready) == 680, "Dispatcher snapshot ABI");

// ====== private_cpu_dispatcher_storage ======

X86_SCHEDULER_LOCAL *x86_scheduler_local_prepare(void);
X86_SCHEDULER_LOCAL *x86_scheduler_local_current(void);
X86_SCHEDULER_LOCAL *x86_scheduler_local_cpu(uint32_t cpu);
#endif
