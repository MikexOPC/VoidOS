#include "../mapping.h"

// ====== x86_vtd_register_view ======

// Intel VT-d5.0 11.4: whitelist VER0/u32,CAP8/u64,ECAP0x10/u64,GSTS0x1c/u32.
volatile unsigned char *x86_vtd_register_view(BOOT_U64 address,BOOT_U32 width)
{
    if ((width!=4 && width!=8) || address<X86_IOMMU_VIRTUAL || (address&(width-1))) return 0;
    BOOT_U64 relative=address-X86_IOMMU_VIRTUAL,offset=relative&8191;
    if (relative>=16*4096 || offset>4096-width || (width==4 && offset!=0 && offset!=0x1c) ||
        (width==8 && offset!=8 && offset!=0x10)) return 0;
    return (volatile unsigned char *)address;
}
