#include "write.h"

// ====== x86_fb_pixel_view ======

volatile BOOT_U32 *x86_fb_pixel_view(BOOT_U64 address)
{
    if ((address & 3) || address < X86_FB_VIRTUAL || address - X86_FB_VIRTUAL >
        4321ULL * 4096 - 4) return 0;
    return (volatile BOOT_U32 *)address;
}
