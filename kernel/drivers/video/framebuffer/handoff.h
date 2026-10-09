#ifndef OS_FRAMEBUFFER_HANDOFF_H
#define OS_FRAMEBUFFER_HANDOFF_H
#include "framebuffer.h"

// ====== fb_handoff_valid ======

static inline int fb_handoff_valid(const OS_FRAMEBUFFER *fb, uint32_t bits)
{
    uint64_t used;
    if (!fb || bits < 32 || bits > 52) return 0;
    if (!(fb->Base | fb->Capacity | fb->Used | fb->Width | fb->Height |
          fb->Stride | fb->Format)) return 1;
    used = fb_descriptor_bytes(fb->Base,fb->Capacity,fb->Width,fb->Height,
                               fb->Stride,fb->Format,bits);
    return used != 0 && fb->Used == used;
}
int fb_handoff_memory_valid(const OS_FRAMEBUFFER *fb, const void *map,
    uint64_t map_bytes, uint64_t descriptor_bytes, uint32_t bits);
#endif
