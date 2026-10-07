#ifndef X86_HPET_KERNEL_DESCRIPTION_H
#define X86_HPET_KERNEL_DESCRIPTION_H

#include "../scan.h"
#include "kernel/firmware/acpi/root.h"
#include "arch/x86/mm/window/page.h"
#include "arch/x86/mm/paging/inventory.h"

/* Output is caller-owned persistent storage, disjoint from all input owners.
 * Success permits ABSENT (output unchanged) or FOUND (validated description).
 * This acquires no HPET MMIO mapping or comparator/IRQ ownership. */
int x86_hpet_kernel_capture(const KERNEL_BOOT_CONTEXT *context,
    X86_PAGE_WINDOW *window, const KERNEL_ACPI_DIRECTORY *directory,
    X86_HPET_DESCRIPTION *output);
int x86_hpet_kernel_initialize(const KERNEL_BOOT_CONTEXT *context,
    X86_PAGE_WINDOW *window, const KERNEL_ACPI_DIRECTORY *directory,
    const X86_PAGING_INVENTORY *inventory, X86_HPET_DESCRIPTION *output);

#endif
