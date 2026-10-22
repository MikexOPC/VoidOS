#include "transport.h"

// ====== x86_vgpu_register_view ======

volatile unsigned char *x86_vgpu_register_view(BOOT_U64 address,uint32_t width)
{
    if ((width!=1 && width!=4) || (address&(width-1)) || address<X86_GPU_VIRTUAL ||
        address-X86_GPU_VIRTUAL>16ULL*4096-width) return 0;
    return (volatile unsigned char *)address;
}
