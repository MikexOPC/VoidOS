#include "cell.h"
#include "../font/font.h"

// ====== encode ======

static BOOT_U32 encode(BOOT_U32 scalar, unsigned char bytes[4])
{
    if (scalar < 128) { bytes[0] = (unsigned char)scalar; return 1; }
    if (scalar < 2048) {
        bytes[0] = (unsigned char)(0xc0 | (scalar >> 6));
        bytes[1] = (unsigned char)(0x80 | (scalar & 63));
        return 2;
    }
    if (scalar < 65536) {
        bytes[0] = (unsigned char)(0xe0 | (scalar >> 12));
        bytes[1] = (unsigned char)(0x80 | ((scalar >> 6) & 63));
        bytes[2] = (unsigned char)(0x80 | (scalar & 63));
        return 3;
    }
    bytes[0] = (unsigned char)(0xf0 | (scalar >> 18));
    bytes[1] = (unsigned char)(0x80 | ((scalar >> 12) & 63));
    bytes[2] = (unsigned char)(0x80 | ((scalar >> 6) & 63));
    bytes[3] = (unsigned char)(0x80 | (scalar & 63));
    return 4;
}

// ====== x86_fb_cell ======

int x86_fb_cell(X86_FB_MAPPING *owner, BOOT_U32 column, BOOT_U32 row, BOOT_U32 scalar,
    BOOT_U32 foreground, BOOT_U32 background, BOOT_U64 *ticks)
{
    unsigned char bytes[4];
    const X86_FB_FONT *font = x86_fb_console_font();
    X86_FB_TEXT text;
    BOOT_U64 start, end, flags;
    int drawn, restored;
    if (!ticks || !owner || owner->Self != (BOOT_U64)owner || owner->Ready != 2 ||
        !owner->Context || !owner->Context->Info.Framebuffer.Base ||
        column >= 240 || row >= 67 || column >= owner->Context->Info.Framebuffer.Width / font->Width ||
        row >= owner->Context->Info.Framebuffer.Height / font->Height ||
        (foreground | background) & 0xff000000U || !x86_fb_font_scalar(scalar) ||
        !x86_fb_cell_clock_available()) return 0;
    text.Text = bytes;
    text.Bytes = encode(scalar, bytes);
    text.Capacity = sizeof(bytes);
    text.X = column * font->Width; text.Y = row * font->Height;
    text.Foreground = foreground; text.Background = background;
    start = x86_fb_cell_stamp();
    flags = x86_fb_cell_suspend();
    drawn = x86_fb_text(owner, &text, font);
    restored = x86_fb_cell_resume(flags);
    end = x86_fb_cell_stamp();
    if (!restored || end <= start) return 0;
    *ticks = end - start;
    return drawn;
}

// ====== x86_fb_group_pointer ======

int x86_fb_group_pointer(X86_FB_MAPPING *owner, BOOT_U32 column, BOOT_U32 row,
    const BOOT_U32 *scalars, BOOT_U32 count, BOOT_U32 foreground, BOOT_U32 background,
    BOOT_U64 *ticks, BOOT_U32 caret, const KERNEL_POINTER_IMAGE *pointer)
{
    unsigned char bytes[32];
    const X86_FB_FONT *font = x86_fb_console_font();
    X86_FB_TEXT text;
    BOOT_U32 index, length = 0, columns;
    BOOT_U64 start, end, flags;
    int drawn, restored;
    if (!ticks || !scalars || ((BOOT_U64)scalars & 3) || !count || count > 8 || (caret != ~0U && caret >= count) ||
        (BOOT_U64)scalars > ~0ULL - count * sizeof(*scalars) ||
        !owner || owner->Self != (BOOT_U64)owner || owner->Ready != 2 || !owner->Context ||
        !owner->Context->Info.Framebuffer.Base || row >= 67 ||
        row >= owner->Context->Info.Framebuffer.Height / font->Height ||
        (foreground | background) & 0xff000000U || !x86_fb_cell_clock_available()) return 0;
    columns = owner->Context->Info.Framebuffer.Width / font->Width;
    if (column >= 240 || count > 240 - column || column >= columns || count > columns - column) return 0;
    for (index = 0; index < count; ++index) {
        if (!x86_fb_font_scalar(scalars[index])) return 0;
        length += encode(scalars[index], bytes + length);
    }
    text.Text = bytes; text.Bytes = length; text.Capacity = sizeof(bytes);
    text.X = column * font->Width; text.Y = row * font->Height;
    text.Foreground = foreground; text.Background = background;
    start = x86_fb_cell_stamp(); flags = x86_fb_cell_suspend();
    drawn = pointer ? x86_fb_text_pointer(owner, &text, font, caret, pointer) :
        x86_fb_text_caret(owner, &text, font, caret);
    restored = x86_fb_cell_resume(flags); end = x86_fb_cell_stamp();
    if (!restored || end <= start) return 0;
    *ticks = end - start;
    return drawn;
}

// ====== x86_fb_group_caret ======

int x86_fb_group_caret(X86_FB_MAPPING *owner, BOOT_U32 column, BOOT_U32 row,
    const BOOT_U32 *scalars, BOOT_U32 count, BOOT_U32 foreground, BOOT_U32 background,
    BOOT_U64 *ticks, BOOT_U32 caret)
{
    return x86_fb_group_pointer(owner, column, row, scalars, count, foreground, background, ticks, caret, NULL);
}

// ====== x86_fb_group ======

int x86_fb_group(X86_FB_MAPPING *owner, BOOT_U32 column, BOOT_U32 row,
    const BOOT_U32 *scalars, BOOT_U32 count, BOOT_U32 foreground, BOOT_U32 background, BOOT_U64 *ticks)
{
    return x86_fb_group_caret(owner, column, row, scalars, count, foreground, background, ticks, ~0U);
}
