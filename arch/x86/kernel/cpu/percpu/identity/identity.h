#ifndef OS_X86_CPU_IDENTITY_H
#define OS_X86_CPU_IDENTITY_H
#include "../percpu.h"
#include "arch/x86/kernel/interrupts/apic/profile.h"

// ====== identity_contract ======

typedef struct {
    uint32_t  Index, Id, Uid, Kind;
} X86_CPU_IDENTITY;
typedef struct {
    uint64_t  Self, BspContext;
    uint32_t  Count, Offline, Ignored, Ready;
    X86_CPU_IDENTITY  Cpus[X86_PERCPU_CAPACITY];
} X86_CPU_IDENTITIES;
_Static_assert(sizeof(X86_CPU_IDENTITY) == 16 && offsetof(X86_CPU_IDENTITIES, Cpus) == 32 &&
    sizeof(X86_CPU_IDENTITIES) == 1056, "Stable CPU identity ABI");
uint32_t xi_record_state(uint32_t uid, uint32_t id, uint32_t flags, uint32_t kind);
uint32_t xi_distinct(uint32_t uid, uint32_t id, uint32_t other_uid, uint32_t other_id);
int x86_cpu_identity_plan(const KERNEL_MADT_CPU *records, uint32_t count,
    uint32_t bsp_id, uint32_t bsp_uid, X86_CPU_IDENTITIES *output);
int x86_cpu_identity_inputs(const KERNEL_MADT_TOPOLOGY *topology, const X86_APIC_PROFILE *profile, uint32_t bits);
int x86_cpu_identity_initialize(const KERNEL_MADT_TOPOLOGY *topology, const X86_APIC_PROFILE *profile, uint32_t bits);
const X86_CPU_IDENTITIES *x86_cpu_identities(void);
int x86_cpu_identity_report(void);
#endif
