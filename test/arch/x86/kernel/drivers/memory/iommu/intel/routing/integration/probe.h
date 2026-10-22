#ifndef X86_VTD_ROUTING_PROBE_H
#define X86_VTD_ROUTING_PROBE_H
#include "arch/x86/kernel/drivers/memory/iommu/intel/routing/routing.h"
int x86_vtd_routing_probe(X86_ECAM *reader,const KERNEL_ACPI_DIRECTORY *directory,const X86_PCI_TOPOLOGY *topology);
int x86_vtd_routing_matrix(const X86_PCI_TOPOLOGY *topology,const X86_VTD_ROUTE_REQUEST *request);
#endif
