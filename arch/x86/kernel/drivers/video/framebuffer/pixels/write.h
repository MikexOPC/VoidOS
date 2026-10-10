#ifndef OS_X86_FRAMEBUFFER_PIXELS_H
#define OS_X86_FRAMEBUFFER_PIXELS_H
#include "../mapping.h"
int x86_fb_pixel(X86_FB_MAPPING *owner, BOOT_U32 x, BOOT_U32 y,
    unsigned char red, unsigned char green, unsigned char blue);
volatile BOOT_U32 *x86_fb_pixel_view(BOOT_U64 address);
#endif
