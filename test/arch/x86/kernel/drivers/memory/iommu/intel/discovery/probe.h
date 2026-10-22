#ifndef X86_DMAR_KERNEL_PROBE_H
#define X86_DMAR_KERNEL_PROBE_H
#include "arch/x86/mm/window/page.h"
#include "kernel/firmware/acpi/root.h"
#include "arch/x86/kernel/drivers/memory/iommu/intel/discovery/table.h"
const X86_DMAR_INFO *x86_dmar_kernel_information(void);
int x86_dmar_kernel_probe(const KERNEL_BOOT_CONTEXT *context,X86_PAGE_WINDOW *window,const KERNEL_ACPI_DIRECTORY *directory);
#endif
