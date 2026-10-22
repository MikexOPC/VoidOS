#ifndef KERNEL_VGPU_DISCOVERY_PCI_H
#define KERNEL_VGPU_DISCOVERY_PCI_H
#include <stdint.h>

#define KERNEL_VGPU_PCI_ABSENT 0U
#define KERNEL_VGPU_PCI_READY 1U
#define KERNEL_VGPU_PCI_REJECTED 2U
#define KERNEL_VGPU_PCI_UNSUPPORTED 3U

typedef struct {
    uint64_t  Address, Length;
    uint32_t  Bar, Offset, Multiplier, Reserved;
} KERNEL_VGPU_PCI_REGION;

typedef struct {
    KERNEL_VGPU_PCI_REGION  Regions[4];
    uint32_t  Capabilities, Revision, Class, Reserved;
} KERNEL_VGPU_PCI_LAYOUT;

_Static_assert(sizeof(KERNEL_VGPU_PCI_REGION) == 32, "PCI region layout");
_Static_assert(sizeof(KERNEL_VGPU_PCI_LAYOUT) == 144, "PCI discovery layout");

uint32_t kernel_vgpu_pci_probe(const unsigned char *config, uint32_t bytes,
    const uint64_t *sizes, uint32_t size_count, uint32_t physical_bits,
    KERNEL_VGPU_PCI_LAYOUT *output);
uint64_t kernel_vgpu_pci_bar(uint32_t low, uint32_t high, uint64_t size, uint32_t bits);
uint64_t kernel_vgpu_pci_region(uint32_t kind, uint32_t offset, uint32_t length,
    uint32_t multiplier, uint64_t base, uint64_t size);
#endif
