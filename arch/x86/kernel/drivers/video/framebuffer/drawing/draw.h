#ifndef OS_X86_FRAMEBUFFER_DRAW_H
#define OS_X86_FRAMEBUFFER_DRAW_H
#include "../mapping.h"
#define X86_FB_DRAW_PIXELS 4096U
#define X86_FB_TEXT_BYTES 256U
#define X86_FB_TEXT_CELLS 64U
#define X86_FB_FONT_GLYPHS 128U
typedef struct {
    BOOT_U32  Scalar;
    unsigned char  Rows[8];
} X86_FB_GLYPH;
typedef struct {
    const X86_FB_GLYPH  *Glyphs;
    BOOT_U32  Count, Bytes;
    unsigned char  Missing[8];
} X86_FB_FONT;
typedef struct {
    const unsigned char  *Text;
    BOOT_U32  Bytes, Capacity;
    BOOT_U32  X, Y, Foreground, Background;
} X86_FB_TEXT;
int x86_fb_rectangle(X86_FB_MAPPING *owner, BOOT_U32 x, BOOT_U32 y,
    BOOT_U32 width, BOOT_U32 height, BOOT_U32 rgb);
int x86_fb_text(X86_FB_MAPPING *owner, const X86_FB_TEXT *text, const X86_FB_FONT *font);
BOOT_U64 x86_fb_utf8_scalar(BOOT_U32 bytes, BOOT_U32 remaining);
BOOT_U32 x86_fb_font_scalar(BOOT_U32 scalar);
#endif
