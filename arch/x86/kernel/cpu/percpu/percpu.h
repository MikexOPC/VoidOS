#ifndef OS_X86_PERCPU_H
#define OS_X86_PERCPU_H
#include "kernel/sync/lock.h"

// ====== per_cpu_contract ======

#define X86_PERCPU_CAPACITY 64U
#define X86_PERCPU_PREPARED 1U
#define X86_PERCPU_ACTIVE 2U

typedef struct {
    _Alignas(64) uint64_t Self;
    uint64_t  EarlyContext;
    uint32_t  Index, State;
    KERNEL_LOCK_CONTEXT  Locks;
    uint64_t  Reserved;
} X86_CPU_LOCAL;
_Static_assert(offsetof(X86_CPU_LOCAL, Self) == 0, "GS self offset");
_Static_assert(offsetof(X86_CPU_LOCAL, Locks) == 24, "GS lock context offset");
_Static_assert(sizeof(X86_CPU_LOCAL) == 448 && _Alignof(X86_CPU_LOCAL) == 64, "Per-CPU software ABI");
int x86_percpu_bsp_promote(KERNEL_LOCK_CONTEXT *early);
const X86_CPU_LOCAL *x86_percpu_bsp(void);
const X86_CPU_LOCAL *x86_percpu_current_local(void);
KERNEL_LOCK_CONTEXT *x86_percpu_current_context(void);
int x86_percpu_bsp_report(void);
#endif
