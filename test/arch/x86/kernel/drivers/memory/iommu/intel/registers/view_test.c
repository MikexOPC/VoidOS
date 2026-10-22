#include "arch/x86/kernel/drivers/memory/iommu/intel/registers/mapping.h"
#include <stdio.h>

// ====== main ======

int main(void)
{
    unsigned checks=0;
    for (BOOT_U64 offset=0; offset<17*4096; ++offset) for (unsigned width=0; width<=9; ++width) {
        BOOT_U64 local=offset%8192;
        int expected=offset<16*4096 && ((width==4 && (local==0 || local==28)) || (width==8 && (local==8 || local==16)));
        if ((x86_vtd_register_view(X86_IOMMU_VIRTUAL+offset,width)!=0)!=expected) return 1;
        ++checks;
    }
    if (x86_vtd_register_view(X86_IOMMU_VIRTUAL-1,4) || x86_vtd_register_view(~0ULL,8)) return 1;
    printf("PASS %u register view bounds/width/whitelist checks; no MMIO dereference\n",checks);
    return 0;
}
