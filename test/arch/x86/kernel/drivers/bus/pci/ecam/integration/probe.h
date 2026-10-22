#ifndef ECAM_INTEGRATION_PROBE_H
#define ECAM_INTEGRATION_PROBE_H
#include "arch/x86/kernel/drivers/bus/pci/ecam/ecam.h"
int x86_ecam_probe(const KERNEL_BOOT_CONTEXT *context,X86_PAGE_WINDOW *window,const X86_PAGING_INVENTORY *inventory,
    const KERNEL_ACPI_DIRECTORY *directory);
#endif
