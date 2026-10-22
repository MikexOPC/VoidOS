#ifndef X86_PCI_TOPOLOGY_PROBE_H
#define X86_PCI_TOPOLOGY_PROBE_H
#include "arch/x86/kernel/drivers/bus/pci/topology/topology.h"
int x86_pci_topology_probe(X86_ECAM *reader,const KERNEL_ACPI_DIRECTORY *directory);
#endif
