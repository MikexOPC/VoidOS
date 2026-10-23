#include "session.h"

// ====== x86_fb_pixel_view ======

volatile BOOT_U32 *x86_fb_pixel_view(BOOT_U64 address)
{
    const X86_FB_TARGET *target = x86_fb_target();

    if (address & 3) return 0;

    if (target) return address >= target->Base && address - target->Base <= target->Bytes - 4 ? (volatile BOOT_U32 *)address : 0;

    if (address < X86_FB_VIRTUAL || address - X86_FB_VIRTUAL > 4321ULL * 4096 - 4) return 0;

    return (volatile BOOT_U32 *)address;
}
