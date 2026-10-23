#ifndef X86_VTD_DMA_PROBE_H
#define X86_VTD_DMA_PROBE_H
#include "arch/x86/kernel/drivers/memory/iommu/intel/activation/activation.h"
#include "test/arch/x86/kernel/drivers/memory/iommu/intel/dma/edu.h"

// QEMU placement fixed by the campaign: -device edu,addr=05.0 on bus 0.
#define X86_VTD_DMA_DEVICE 5U
#define X86_VTD_DMA_MOVE  64U
// Buffers of the private domain: A and D read-only, B and C read/write; B is revoked in P4.
// D is never read by the device: on an IOTLB hit QEMU v11.1.0 vtd_do_iommu_translate returns the
// cached read-only permission without a walk or a fault record, so the fault needs an uncached page.
#define X86_VTD_DMA_A 0U
#define X86_VTD_DMA_B 1U
#define X86_VTD_DMA_C 2U
#define X86_VTD_DMA_D 3U
#define X86_VTD_DMA_BUFFERS 4U

typedef struct {
    X86_ECAM  *Reader;
    X86_QI  *Qi;
    X86_VTD_DOMAIN  *Domain;
    X86_VTD_ACTIVATION  *Activation;
    EDU_BUS  Bus;
    BOOT_U64  Bar;
} X86_VTD_DMA_SESSION;

int x86_vtd_dma_probe(X86_ECAM *reader, const KERNEL_ACPI_DIRECTORY *directory, const X86_DMAR_INFO *info,
    const X86_PCI_TOPOLOGY *topology);
int x86_vtd_dma_fill(X86_VTD_DMA_SESSION *session, BOOT_U32 index, BOOT_U32 seed);
int x86_vtd_dma_phases(X86_VTD_DMA_SESSION *session);
#endif
