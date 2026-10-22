#ifndef X86_VTD_ACTIVATION_PROBE_H
#define X86_VTD_ACTIVATION_PROBE_H
#include "arch/x86/kernel/drivers/memory/iommu/intel/activation/activation.h"
int x86_vtd_activation_probe(X86_ECAM *reader,const KERNEL_ACPI_DIRECTORY *directory,const X86_DMAR_INFO *info,
    const X86_VTD_ROUTE *route,const X86_VTD_ROUTE_REQUEST *request);
#endif
