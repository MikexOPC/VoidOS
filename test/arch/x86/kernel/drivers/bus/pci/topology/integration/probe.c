#include "probe.h"
#include "kernel/diagnostics/log.h"
#ifdef KERNEL_TEST_IOMMU_ROUTING
#include "test/arch/x86/kernel/drivers/memory/iommu/intel/routing/integration/probe.h"
#endif
static X86_PCI_TOPOLOGY scratch,snapshot;

// ====== number_private ======

static int number_private(const char *name,BOOT_U64 value)
{ return kernel_text(name) && kernel_number(value) && kernel_text("\n"); }

// ====== x86_pci_topology_probe ======

int x86_pci_topology_probe(X86_ECAM *reader,const KERNEL_ACPI_DIRECTORY *directory)
{
    (void)directory;
    if (!kernel_log(1,"KERNEL_PCI_TOPOLOGY_BEGIN") || !x86_pci_topology_native(reader,&scratch,&snapshot) ||
        !x86_pci_topology_valid(&snapshot) || !x86_ecam_current(reader)) return 0;
    if (!number_private("KERNEL_PCI_TOPOLOGY_SNAPSHOT: ",(BOOT_U64)&snapshot) ||
        !number_private("KERNEL_PCI_TOPOLOGY_COUNT: ",snapshot.Count) ||
        !number_private("KERNEL_PCI_TOPOLOGY_BRIDGES: ",snapshot.Bridges) ||
        !number_private("KERNEL_PCI_TOPOLOGY_READS: ",snapshot.Reads)) return 0;
    if (!kernel_log(0,"KERNEL_PCI_TOPOLOGY_OK")) return 0;
#ifdef KERNEL_TEST_IOMMU_ROUTING
    return x86_vtd_routing_probe(reader,directory,&snapshot);
#else
    return 1;
#endif
}
