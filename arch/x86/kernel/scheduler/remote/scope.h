#ifndef X86_SCHEDULER_REMOTE_SCOPE_H
#define X86_SCHEDULER_REMOTE_SCOPE_H
#include "../integration/local.h"
#include "../mapping/native.h"
#include "../admission/admission.h"
#include "arch/x86/kernel/cpu/percpu/resources/paging/authority.h"

typedef struct {
    uint64_t                    Root, Token;
    const KERNEL_PHYSICAL_ALLOCATOR *Pool;
    X86_AP_SNAPSHOT             Arena;
    uint32_t                    Aps, Reserved;
} X86_AP_SCOPE;

// ====== ap_execution_scope ======

int x86_ap_scope_bind(const X86_AP_SCOPE *scope);
void x86_ap_scope_unbind(void);
const X86_AP_SCOPE *x86_ap_scope(void);
int x86_ap_actor(void);
int x86_ap_gate(const KERNEL_PHYSICAL_ALLOCATOR *pool, uint64_t root, const X86_TASK_CPU *cpu);
int x86_ap_dispatcher_gate(void);
int x86_ap_dispatcher_authority(void);
int x86_ap_dispatchers_start(KERNEL_PHYSICAL_ALLOCATOR *pool, uint64_t root);
void x86_ap_dispatcher_run(void);
void x86_ap_dispatcher_sleep(void);
extern const unsigned char x86_ap_dispatcher_sleep_resume[];
#endif
