#include "draw.h"
#include "../pixels/session.h"
#include "arch/x86/kernel/exceptions/terminal.h"
#if defined(KERNEL_TEST_CONSOLE_DRAW)
#include "test/kernel/console/video/profiling/profile.h"
#define DRAW_MARK(phase) do { if (!kernel_console_draw_mark(phase)) return 0; } while (0)
#else
#define DRAW_MARK(phase) ((void)0)
#endif

// ====== color_word ======

static BOOT_U32 color_word(BOOT_U32 format, BOOT_U32 rgb)
{
    return fb_color_word(format, (unsigned char)(rgb >> 16),
        (unsigned char)(rgb >> 8), (unsigned char)rgb);
}

// ====== x86_fb_rectangle ======

int x86_fb_rectangle(X86_FB_MAPPING *owner, BOOT_U32 x, BOOT_U32 y,
    BOOT_U32 width, BOOT_U32 height, BOOT_U32 rgb)
{
    X86_FB_PIXEL_SESSION session;
    BOOT_U32 row, column, color;
    if (rgb & 0xff000000U || !x86_fb_pixels_begin(owner, x, y, width, height, &session)) return 0;
    color = color_word(session.Surface.Format, rgb);
    for (row = 0; row < height; ++row) for (column = 0; column < width; ++column)
        if (!x86_fb_pixels_store(&session, x + column, y + row, color)) return 0;
    return x86_fb_pixels_end(&session);
}

// ====== font_valid ======

static int font_valid(const X86_FB_FONT *font)
{
    BOOT_U32 index, visible = 0;
    if (!x86_fb_font_height(font->Height) || (font->Width != 8 && font->Width != X86_FB_CELL_WIDTH) || !font->Glyphs || ((BOOT_U64)font->Glyphs & 3) || !font->Count ||
        font->Count > X86_FB_FONT_GLYPHS || font->Bytes < font->Count * sizeof(X86_FB_GLYPH) ||
        (BOOT_U64)font->Glyphs > ~0ULL - font->Bytes) return 0;
    for (index = 0; index < font->Height; ++index) visible |= font->Missing[index];
    if (!visible) return 0;
    for (index = 0; index < font->Count; ++index)
        if (!x86_fb_font_scalar(font->Glyphs[index].Scalar) ||
            (index && font->Glyphs[index - 1].Scalar >= font->Glyphs[index].Scalar)) return 0;
    return 1;
}

// ====== text_plan ======

static BOOT_U32 text_plan(const X86_FB_TEXT *text, const X86_FB_FONT *font,
    unsigned short rows[X86_FB_TEXT_CELLS][X86_FB_CELL_HEIGHT])
{
    BOOT_U32 offset = 0, count = 0, byte, glyph, row;
    if (!text->Text || !text->Bytes || text->Bytes > X86_FB_TEXT_BYTES ||
        text->Capacity < text->Bytes || (BOOT_U64)text->Text > ~0ULL - text->Bytes ||
        !font_valid(font)) return 0;
    while (offset < text->Bytes) {
        BOOT_U32 packed = 0;
        BOOT_U64 decoded;
        const unsigned short *bitmap;
        if (count == X86_FB_TEXT_CELLS) return 0;
        for (byte = 0; byte < 4 && byte < text->Bytes - offset; ++byte)
            packed |= (BOOT_U32)text->Text[offset + byte] << (byte * 8);
        decoded = x86_fb_utf8_scalar(packed, text->Bytes - offset);
        if (decoded == ~0ULL) return 0;
        bitmap = font->Missing;
        for (glyph = 0; glyph < font->Count; ++glyph)
            if (font->Glyphs[glyph].Scalar == (BOOT_U32)decoded) {
                bitmap = font->Glyphs[glyph].Rows; break;
            }
        for (row = 0; row < font->Height; ++row) rows[count][row] = bitmap[row];
        offset += (BOOT_U32)(decoded >> 32); ++count;
    }
    return count;
}

// ====== x86_fb_text_pointer ======

int x86_fb_text_pointer(X86_FB_MAPPING *owner, const X86_FB_TEXT *input,
    const X86_FB_FONT *input_font, BOOT_U32 caret, const KERNEL_POINTER_IMAGE *input_pointer)
{
    KERNEL_POINTER_IMAGE pointer = {0};
    X86_FB_TEXT text;
    X86_FB_FONT font;
    X86_FB_PIXEL_SESSION session;
    unsigned short rows[X86_FB_TEXT_CELLS][X86_FB_CELL_HEIGHT];
    BOOT_U32 count, cell, row, column, foreground, background;
    if (!input || !input_font || ((BOOT_U64)input & 7) || ((BOOT_U64)input_font & 7) ||
        !x86_exception_environment()) return 0;
    text = *input; font = *input_font;
    if (input_pointer) {
        if ((BOOT_U64)input_pointer & 3) return 0;
        pointer = *input_pointer;
        if (pointer.Visible > 1 || (pointer.Visible && (!owner || !owner->Context ||
            owner->Context->Info.Framebuffer.Width < 18 || owner->Context->Info.Framebuffer.Height < 18 ||
            pointer.X > owner->Context->Info.Framebuffer.Width - 18 ||
            pointer.Y > owner->Context->Info.Framebuffer.Height - 18))) return 0;
    }
    if ((text.Foreground | text.Background) & 0xff000000U) return 0;
    DRAW_MARK(1);
    count = text_plan(&text, &font, rows);
    DRAW_MARK(2);
    if (!count || (caret != ~0U && (caret >= count || font.Height != X86_FB_CELL_HEIGHT ||
        font.Width != X86_FB_CELL_WIDTH))) return 0;
    if (caret != ~0U)
        rows[caret][X86_FB_CELL_HEIGHT - 2] = rows[caret][X86_FB_CELL_HEIGHT - 1] = (1U << X86_FB_CELL_WIDTH) - 1;
    if (input_pointer) {
        if (font.Height != X86_FB_CELL_HEIGHT || font.Width != X86_FB_CELL_WIDTH) return 0;
        return x86_fb_cursor_text(owner, &text, rows, count, &pointer);
    }
    if (!x86_fb_pixels_begin(owner, text.X, text.Y, count * font.Width, font.Height, &session)) return 0;
    DRAW_MARK(3);
    foreground = color_word(session.Surface.Format, text.Foreground);
    background = color_word(session.Surface.Format, text.Background);
    for (cell = 0; cell < count; ++cell) for (row = 0; row < font.Height; ++row)
        for (column = 0; column < font.Width; ++column) {
            BOOT_U32 x = text.X + cell * font.Width + column, y = text.Y + row;
            BOOT_U32 sample = pointer.Visible && x >= pointer.X && y >= pointer.Y ?
                kernel_pointer_sample(x - pointer.X, y - pointer.Y) : 0;
            BOOT_U32 color = (rows[cell][row] & (1U << (font.Width - 1 - column))) ? foreground : background;
            if (sample) color = kernel_pointer_blend(sample, color);
            if (!x86_fb_pixels_store(&session, x, y, color)) return 0;
        }
#if defined(KERNEL_TEST_CONSOLE_DRAW)
    DRAW_MARK(4);
    if (!x86_fb_pixels_end(&session)) return 0;
    DRAW_MARK(5);
    return 1;
#else
    return x86_fb_pixels_end(&session);
#endif
}

// ====== x86_fb_text_caret ======

int x86_fb_text_caret(X86_FB_MAPPING *owner, const X86_FB_TEXT *input,
    const X86_FB_FONT *font, BOOT_U32 caret)
{
    return x86_fb_text_pointer(owner, input, font, caret, NULL);
}

// ====== x86_fb_text ======

int x86_fb_text(X86_FB_MAPPING *owner, const X86_FB_TEXT *input, const X86_FB_FONT *font)
{
    return x86_fb_text_caret(owner, input, font, ~0U);
}
