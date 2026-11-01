#include "identity.h"

// ====== x86_cpu_identity_inputs ======

int x86_cpu_identity_inputs(const KERNEL_MADT_TOPOLOGY *topology, const X86_APIC_PROFILE *profile, uint32_t bits) {
    return kernel_madt_validate(topology, bits) && x86_apic_profile_valid(topology, profile, bits);
}
