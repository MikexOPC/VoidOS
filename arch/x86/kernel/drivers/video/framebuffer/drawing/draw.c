#include "draw.h"
#include "../pixels/session.h"
#include "arch/x86/kernel/exceptions/terminal.h"

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
    if (!font->Glyphs || ((BOOT_U64)font->Glyphs & 3) || !font->Count ||
        font->Count > X86_FB_FONT_GLYPHS || font->Bytes < font->Count * sizeof(X86_FB_GLYPH) ||
        (BOOT_U64)font->Glyphs > ~0ULL - font->Bytes) return 0;
    for (index = 0; index < 8; ++index) visible |= font->Missing[index];
    if (!visible) return 0;
    for (index = 0; index < font->Count; ++index)
        if (!x86_fb_font_scalar(font->Glyphs[index].Scalar) ||
            (index && font->Glyphs[index - 1].Scalar >= font->Glyphs[index].Scalar)) return 0;
    return 1;
}

// ====== text_plan ======

static BOOT_U32 text_plan(const X86_FB_TEXT *text, const X86_FB_FONT *font,
    unsigned char rows[X86_FB_TEXT_CELLS][8])
{
    BOOT_U32 offset = 0, count = 0, byte, glyph, row;
    if (!text->Text || !text->Bytes || text->Bytes > X86_FB_TEXT_BYTES ||
        text->Capacity < text->Bytes || (BOOT_U64)text->Text > ~0ULL - text->Bytes ||
        !font_valid(font)) return 0;
    while (offset < text->Bytes) {
        BOOT_U32 packed = 0;
        BOOT_U64 decoded;
        const unsigned char *bitmap;
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
        for (row = 0; row < 8; ++row) rows[count][row] = bitmap[row];
        offset += (BOOT_U32)(decoded >> 32); ++count;
    }
    return count;
}

// ====== x86_fb_text ======

int x86_fb_text(X86_FB_MAPPING *owner, const X86_FB_TEXT *input, const X86_FB_FONT *input_font)
{
    X86_FB_TEXT text;
    X86_FB_FONT font;
    X86_FB_PIXEL_SESSION session;
    unsigned char rows[X86_FB_TEXT_CELLS][8];
    BOOT_U32 count, cell, row, column, foreground, background;
    if (!input || !input_font || ((BOOT_U64)input & 7) || ((BOOT_U64)input_font & 7) ||
        !x86_exception_environment()) return 0;
    text = *input; font = *input_font;
    if ((text.Foreground | text.Background) & 0xff000000U) return 0;
    count = text_plan(&text, &font, rows);
    if (!count || !x86_fb_pixels_begin(owner, text.X, text.Y, count * 8, 8, &session)) return 0;
    foreground = color_word(session.Surface.Format, text.Foreground);
    background = color_word(session.Surface.Format, text.Background);
    for (cell = 0; cell < count; ++cell) for (row = 0; row < 8; ++row)
        for (column = 0; column < 8; ++column)
            if (!x86_fb_pixels_store(&session, text.X + cell * 8 + column, text.Y + row,
                (rows[cell][row] & (0x80U >> column)) ? foreground : background)) return 0;
    return x86_fb_pixels_end(&session);
}
