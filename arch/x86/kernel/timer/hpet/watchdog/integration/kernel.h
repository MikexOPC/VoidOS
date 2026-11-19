#ifndef X86_WATCHDOG_KERNEL_INTEGRATION_H
#define X86_WATCHDOG_KERNEL_INTEGRATION_H
#include "../watchdog.h"
#include "arch/x86/mm/arena/objects.h"
typedef struct {
    X86_WATCHDOG_ENV              Environment;
    const KERNEL_ACPI_DIRECTORY  *Directory;
} X86_WATCHDOG_KERNEL_RUNTIME;

int x86_watchdog_kernel_prepare(X86_WATCHDOG_KERNEL_RUNTIME *runtime,
    const KERNEL_BOOT_CONTEXT *context, X86_PAGE_WINDOW *window,
    const X86_OBJECT_ARENA *arena, const KERNEL_OBJECT_HANDLE *acpi, const KERNEL_OBJECT_HANDLE *madt,
    const X86_PAGING_INVENTORY *inventory, const X86_APIC_PROFILE *profile,
    const X86_APIC_CAPABILITIES *caps, const X86_HPET_DESCRIPTION *description);
void x86_watchdog_kernel_dispatch(const X86_WATCHDOG_KERNEL_RUNTIME *runtime);

int x86_watchdog_kernel_start(const KERNEL_BOOT_CONTEXT *context, X86_PAGE_WINDOW *window,
    const X86_OBJECT_ARENA *arena, const KERNEL_OBJECT_HANDLE *acpi, const KERNEL_OBJECT_HANDLE *madt,
    const X86_PAGING_INVENTORY *inventory, const X86_APIC_PROFILE *profile,
    const X86_APIC_CAPABILITIES *caps, const X86_HPET_DESCRIPTION *description);
#endif
