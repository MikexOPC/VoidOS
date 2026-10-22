#ifndef KERNEL_VGPU_DISCOVERY_HANDOFF_H
#define KERNEL_VGPU_DISCOVERY_HANDOFF_H
#include "pci.h"

typedef struct {
    uint32_t  Present, Segment, Bus, Device, Function, Reserved;
    unsigned char  Config[256];
    uint64_t  Sizes[6];
} KERNEL_VGPU_BOOT;
_Static_assert(sizeof(KERNEL_VGPU_BOOT) == 328, "Copied PCI boot metadata ABI");
int kernel_vgpu_boot_valid(const KERNEL_VGPU_BOOT *info, uint32_t physical_bits);
#endif
