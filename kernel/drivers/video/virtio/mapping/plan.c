#include "plan.h"
#include "lib/memory/address_ranges.h"

// ====== kernel_vgpu_plan ======

int kernel_vgpu_plan(const KERNEL_VGPU_BOOT *boot, uint32_t bits, KERNEL_VGPU_PAGE_PLAN *output)
{
    KERNEL_VGPU_PCI_LAYOUT layout;
    KERNEL_VGPU_RANGES ranges = {0};
    KERNEL_VGPU_PAGE_PLAN draft;
    if (!boot || !output || ((uintptr_t)output & 7) ||
        os_address_ranges_overlap((uintptr_t)boot,sizeof(*boot),(uintptr_t)output,sizeof(*output)) ||
        !kernel_vgpu_boot_valid(boot,bits)) return 0;
    if (boot->Present) {
        if (kernel_vgpu_pci_probe(boot->Config,256,boot->Sizes,6,bits,&layout) != KERNEL_VGPU_PCI_READY) return 0;
        for (uint32_t region=0; region<4; ++region) {
            ranges.Address[region]=layout.Regions[region].Address;
            ranges.Length[region]=layout.Regions[region].Length;
        }
    }
    draft=kernel_vgpu_pages(ranges,bits);
    if (draft.Valid != 1 || draft.Count > KERNEL_VGPU_MAX_PAGES) return 0;
    *output=draft;
    return 1;
}
