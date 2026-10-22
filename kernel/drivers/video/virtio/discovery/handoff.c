#include "handoff.h"

// ====== kernel_vgpu_boot_valid ======

int kernel_vgpu_boot_valid(const KERNEL_VGPU_BOOT *info, uint32_t physical_bits)
{
    KERNEL_VGPU_PCI_LAYOUT layout;
    if (!info || physical_bits < 32 || physical_bits > 52) return 0;
    if (!info->Present) {
        for (uint32_t byte = 0; byte < sizeof(*info); ++byte)
            if (((const unsigned char *)info)[byte]) return 0;
        return 1;
    }
    if (info->Present != 1 || info->Reserved || info->Segment ||
        info->Bus > 255 || info->Device > 31 || info->Function > 7) return 0;
    return kernel_vgpu_pci_probe(info->Config,256,info->Sizes,6,physical_bits,&layout)
        == KERNEL_VGPU_PCI_READY;
}
