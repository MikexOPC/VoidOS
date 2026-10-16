#ifndef OS_X86_FRAMEBUFFER_DRAW_H
#define OS_X86_FRAMEBUFFER_DRAW_H
#include "../mapping.h"
#include "kernel/console/video/pointer/shape.h"
#define X86_FB_DRAW_PIXELS 4096U
#define X86_FB_TEXT_BYTES 256U
#define X86_FB_TEXT_CELLS 64U
#define X86_FB_FONT_GLYPHS 128U
/* Console cell: the console font is 10x20 and the pointer/caret path requires exactly it.
   A glyph row holds Width bits, bit Width-1 on the left; other fonts may be 8 wide. */
#define X86_FB_CELL_WIDTH 10U
#define X86_FB_CELL_HEIGHT 20U
typedef struct {
    BOOT_U32  Scalar;
    unsigned short  Rows[X86_FB_CELL_HEIGHT];
} X86_FB_GLYPH;
typedef struct {
    const X86_FB_GLYPH  *Glyphs;
    BOOT_U32  Count, Bytes;
    unsigned short  Missing[X86_FB_CELL_HEIGHT];
    BOOT_U32  Height, Width;
} X86_FB_FONT;
typedef struct {
    const unsigned char  *Text;
    BOOT_U32  Bytes, Capacity;
    BOOT_U32  X, Y, Foreground, Background;
} X86_FB_TEXT;
int x86_fb_rectangle(X86_FB_MAPPING *owner, BOOT_U32 x, BOOT_U32 y,
    BOOT_U32 width, BOOT_U32 height, BOOT_U32 rgb);
int x86_fb_text_caret(X86_FB_MAPPING *owner, const X86_FB_TEXT *text,
    const X86_FB_FONT *font, BOOT_U32 caret);
int x86_fb_text_pointer(X86_FB_MAPPING *owner, const X86_FB_TEXT *text,
    const X86_FB_FONT *font, BOOT_U32 caret, const KERNEL_POINTER_IMAGE *pointer);
int x86_fb_text(X86_FB_MAPPING *owner, const X86_FB_TEXT *text, const X86_FB_FONT *font);
BOOT_U64 x86_fb_utf8_scalar(BOOT_U32 bytes, BOOT_U32 remaining);
BOOT_U32 x86_fb_font_scalar(BOOT_U32 scalar);
BOOT_U32 x86_fb_font_height(BOOT_U32 height);
int x86_fb_cursor_text(X86_FB_MAPPING *owner, const X86_FB_TEXT *text,
    const unsigned short rows[X86_FB_TEXT_CELLS][X86_FB_CELL_HEIGHT], BOOT_U32 count,
    const KERNEL_POINTER_IMAGE *pointer);
#endif
