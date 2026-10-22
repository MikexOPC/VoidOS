#include "native.h"

// ====== x86_vgpu_queue_view ======

volatile unsigned char *x86_vgpu_queue_view(BOOT_U64 address,uint32_t width)
{
    if ((width!=1 && width!=2 && width!=4) || (address&(width-1)) || address<X86_GPU_VIRTUAL ||
        address-X86_GPU_VIRTUAL>16ULL*4096-width) return 0;
    return (volatile unsigned char *)address;
}
