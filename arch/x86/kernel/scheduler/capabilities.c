#include "context.h"

// ====== cpu_valid_private ======

static int cpu_valid_private(const X86_TASK_CPU *cpu, uint64_t expected_root, uint32_t bsp_id, uint32_t bsp)
{
    const uint32_t features = (1U << 9) | (1U << 24) | (1U << 25) | (1U << 26);
    if (!cpu || cpu->reserved || cpu->cpl || cpu->max_leaf < 1 || bsp_id > 255 ||
        cpu->apic_id != bsp_id || (cpu->features & features) != features ||
        !expected_root || expected_root >= (UINT64_C(1) << 52) ||
        (expected_root & 4095) || cpu->cr3 != expected_root) return 0;
    // Intel SDM 093 Vol.1 ch.21 CPUID(1).EDX: APIC9, FXSR24, SSE25, SSE2 26.
    // Vol.3A §§2.5/5.5.2: CR0.PE0/WP16/PG31 required; EM2/TS3 zero.
    // CR3 base is below the architectural 52-bit maximum (actual width gated at boot).
    // CR4.OSFXSR9 required; LA57=12/CET=23 are outside this four-level/raw-RET v0.
    // Vol.1 §§3.4.3.2-.3: IF9/DF10; Vol.3A §13.4.4/Fig13-5: MSR1Bh.BSP8.
    if (!(cpu->cr0 & 1) || (cpu->cr0 & 12) || (cpu->cr0 & ((UINT64_C(1) << 16) | (UINT64_C(1) << 31))) !=
        ((UINT64_C(1) << 16) | (UINT64_C(1) << 31)) || !(cpu->cr4 & (UINT64_C(1) << 9)) ||
        (cpu->cr4 & ((UINT64_C(1) << 12) | (UINT64_C(1) << 23))) ||
        (cpu->flags & ((UINT64_C(1) << 9) | (UINT64_C(1) << 10))) ||
        ((cpu->apic_base >> 8) & 1) != bsp) return 0;
    return 1;
}

// ====== x86_scheduler_cpu_valid ======

int x86_scheduler_cpu_valid(const X86_TASK_CPU *cpu, uint64_t root, uint32_t id)
{
    return cpu_valid_private(cpu,root,id,1);
}

// ====== x86_scheduler_ap_cpu_valid ======

// Intel SDM 078US §11.4.4: IA32_APIC_BASE.BSP8 is clear on APs.
int x86_scheduler_ap_cpu_valid(const X86_TASK_CPU *cpu, uint64_t root, uint32_t id)
{
    return cpu_valid_private(cpu,root,id,0);
}
