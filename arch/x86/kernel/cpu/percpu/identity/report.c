#include "identity.h"
#include "kernel/diagnostics/log.h"

// ====== x86_cpu_identity_report ======

int x86_cpu_identity_report(void) {
    const X86_CPU_IDENTITIES *snapshot = x86_cpu_identities();
    if (!snapshot || !kernel_text("KERNEL_CPU_IDENTITIES: ") || !kernel_number((uintptr_t)snapshot) ||
        !kernel_text("\nKERNEL_CPU_ENABLED: ") || !kernel_number(snapshot->Count) ||
        !kernel_text("\nKERNEL_CPU_OFFLINE: ") || !kernel_number(snapshot->Offline) ||
        !kernel_text("\nKERNEL_CPU_IGNORED: ") || !kernel_number(snapshot->Ignored) || !kernel_text("\n")) return 0;
    for (uint32_t index = 0; index < snapshot->Count; ++index) {
        const X86_CPU_IDENTITY *cpu = &snapshot->Cpus[index];
        if (!kernel_text("KERNEL_CPU_INDEX: ") || !kernel_number(cpu->Index) ||
            !kernel_text("\nKERNEL_CPU_APIC_ID: ") || !kernel_number(cpu->Id) ||
            !kernel_text("\nKERNEL_CPU_ACPI_UID: ") || !kernel_number(cpu->Uid) ||
            !kernel_text("\nKERNEL_CPU_MADT_KIND: ") || !kernel_number(cpu->Kind) || !kernel_text("\n")) return 0;
    }
    return kernel_log(0, "KERNEL_CPU_IDENTITIES_READY");
}
