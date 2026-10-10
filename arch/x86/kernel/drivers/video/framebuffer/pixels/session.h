#ifndef OS_X86_FB_PIXEL_SESSION_H
#define OS_X86_FB_PIXEL_SESSION_H
#include "write.h"
typedef struct {
    X86_FB_MAPPING  *Owner;
    OS_FRAMEBUFFER  Surface;
    BOOT_U64  Base;
    BOOT_U32  X, Y, Width, Height;
} X86_FB_PIXEL_SESSION;
int x86_fb_pixels_begin(X86_FB_MAPPING *owner, BOOT_U32 x, BOOT_U32 y,
    BOOT_U32 width, BOOT_U32 height, X86_FB_PIXEL_SESSION *session);
int x86_fb_pixels_store(const X86_FB_PIXEL_SESSION *session,
    BOOT_U32 x, BOOT_U32 y, BOOT_U32 color);
int x86_fb_pixels_end(const X86_FB_PIXEL_SESSION *session);
BOOT_U32 x86_fb_rect_valid(BOOT_U64 surface, BOOT_U64 origin, BOOT_U64 extent);
#endif
