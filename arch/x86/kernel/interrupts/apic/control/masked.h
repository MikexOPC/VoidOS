#ifndef OS_X86_APIC_MASKED_H
#define OS_X86_APIC_MASKED_H
#include "arch/x86/kernel/interrupts/apic/probe.h"
BOOT_U32 x86_irq_lvt(BOOT_U32 maximum, BOOT_U32 index);
BOOT_U32 x86_irq_nmi_lint(BOOT_U32 lint, BOOT_U32 flags);
int x86_apic_control_read(BOOT_U32 offset, BOOT_U32 *output);
int x86_apic_control_write(BOOT_U32 offset, BOOT_U32 value);
int x86_apic_quiesce(const KERNEL_BOOT_CONTEXT *context, X86_PAGE_WINDOW *window,
    const X86_PAGING_INVENTORY *inventory, const KERNEL_MADT_TOPOLOGY *topology,
    const X86_APIC_PROFILE *profile, const X86_APIC_CAPABILITIES *caps);
#endif
