#include "identity.h"
#include "../internal.h"
static X86_CPU_IDENTITIES identities;

// ====== initialize_private ======

static int initialize_private(const KERNEL_MADT_TOPOLOGY *topology, const X86_APIC_PROFILE *profile, uint32_t bits) {
    const X86_CPU_LOCAL *owner = x86_percpu_bsp();
    if (!owner || identities.Ready || owner->Locks.Depth ||
        !ks_span(topology, sizeof(*topology)) || !ks_span(profile, sizeof(*profile)) ||
        ks_overlap(topology, sizeof(*topology), &identities, sizeof(identities)) ||
        ks_overlap(profile, sizeof(*profile), &identities, sizeof(identities)) ||
        !x86_cpu_identity_inputs(topology, profile, bits)) return 0;
    for (uint32_t byte = 0; byte < sizeof(identities); ++byte)
        if (((const unsigned char *)&identities)[byte]) return 0;
    if (!x86_cpu_identity_plan(topology->Cpus, topology->CpuCount, profile->InitialId, profile->Uid, &identities)) return 0;
    identities.BspContext = (uintptr_t)&owner->Locks;
    identities.Ready = 2;
    return 1;
}

// ====== x86_cpu_identity_initialize ======

int x86_cpu_identity_initialize(const KERNEL_MADT_TOPOLOGY *topology, const X86_APIC_PROFILE *profile, uint32_t bits) {
    uint64_t saved_if = ks_irq_save();
    int result = initialize_private(topology, profile, bits);
    ks_irq_restore(saved_if);
    return result;
}

// ====== current_private ======

static const X86_CPU_IDENTITIES *current_private(void) {
    const X86_CPU_LOCAL *owner = x86_percpu_bsp();
    if (!owner || identities.Ready != 2 || identities.Self != (uintptr_t)&identities ||
        identities.BspContext != (uintptr_t)&owner->Locks || !identities.Count ||
        identities.Count > X86_PERCPU_CAPACITY || identities.Offline > X86_PERCPU_CAPACITY ||
        identities.Ignored > X86_PERCPU_CAPACITY ||
        identities.Count + identities.Offline + identities.Ignored > X86_PERCPU_CAPACITY) return NULL;
    for (uint32_t index = 0; index < X86_PERCPU_CAPACITY; ++index) {
        const X86_CPU_IDENTITY *cpu = &identities.Cpus[index];
        if (index >= identities.Count) {
            if (cpu->Index || cpu->Id || cpu->Uid || cpu->Kind) return NULL;
            continue;
        }
        if (cpu->Index != index || xi_record_state(cpu->Uid, cpu->Id, 1, cpu->Kind) != 3) return NULL;
        for (uint32_t previous = 0; previous < index; ++previous)
            if (!xi_distinct(cpu->Uid, cpu->Id, identities.Cpus[previous].Uid, identities.Cpus[previous].Id)) return NULL;
    }
    return &identities;
}

// ====== x86_cpu_identities ======

const X86_CPU_IDENTITIES *x86_cpu_identities(void) {
    uint64_t saved_if = ks_irq_save();
    const X86_CPU_IDENTITIES *result = current_private();
    ks_irq_restore(saved_if);
    return result;
}
