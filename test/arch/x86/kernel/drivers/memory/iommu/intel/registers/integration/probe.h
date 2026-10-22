#ifndef X86_VTD_KERNEL_PROBE_H
#define X86_VTD_KERNEL_PROBE_H
#include "arch/x86/kernel/drivers/memory/iommu/intel/registers/mapping.h"
int x86_vtd_kernel_probe(const KERNEL_BOOT_CONTEXT *context,X86_PAGE_WINDOW *window,const X86_LAPIC_MAPPING *lapic,
    const X86_DMAR_INFO *description,X86_PAGING_INVENTORY *inventory);
#endif
