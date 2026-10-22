#ifndef X86_VTD_DOMAIN_PROBE_H
#define X86_VTD_DOMAIN_PROBE_H
#include "arch/x86/kernel/drivers/memory/iommu/intel/domains/domain.h"
int x86_vtd_domain_probe(const KERNEL_BOOT_CONTEXT *context,X86_PAGE_WINDOW *window,const X86_LAPIC_MAPPING *lapic,
    X86_PAGING_INVENTORY *inventory,const X86_DMAR_INFO *description);
#endif
