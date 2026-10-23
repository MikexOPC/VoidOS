#include "../lifecycle/fixture.h"
#include "../../../../../../../../arch/x86/kernel/drivers/video/framebuffer/pixels/write.h"
#include "../../../../../../../../arch/x86/kernel/drivers/video/framebuffer/drawing/draw.h"
#include "../../../../../../../../arch/x86/kernel/drivers/video/framebuffer/pixels/session.h"
#include "../../../../../../../../arch/x86/kernel/drivers/video/framebuffer/font/font.h"
#include <string.h>
#include "pointer_test.h"
static BOOT_U32 words[4100], visits, fail_view;
// V2c redirected target: a second pixel store at its own virtual base.
#define TARGET_BASE 0x0000720000000000ULL
static BOOT_U32 target_words[4100], target_valid = 1;
static BOOT_U32 fail_after, corrupt_after;
#define CHECK(expression) do { if (!(expression)) return __LINE__; } while (0)

// ====== x86_fb_pixel_view ======

volatile BOOT_U32 *x86_fb_pixel_view(BOOT_U64 address)
{
    BOOT_U64 offset;
    ++visits;
    if (corrupt_after && visits == corrupt_after) cpu.Pat ^= 1;
    if (fail_after && visits == fail_after) return 0;
    if (fail_view || (address & 3)) return 0;
    if (address >= TARGET_BASE && address - TARGET_BASE < sizeof(target_words)) return &target_words[(address - TARGET_BASE) / 4];
    if (address < X86_FB_VIRTUAL) return 0;
    offset = (address - X86_FB_VIRTUAL) / 4;
    return offset < 4100 ? &words[offset] : 0;
}

// ====== setup ======

static int setup(void)
{
    visits = fail_view = 0;
    fail_after = corrupt_after = 0;
    memset(words, 0xa5, sizeof(words));
    return fixture() && x86_fb_bind(&context, &window, &lapic, &inventory, &owner);
}

// ====== pixel_colors ======

static int pixel_colors(void)
{
    BOOT_U32 format, x, y, channel;
    for (format = 0; format < 2; ++format) {
        CHECK(setup());
        CHECK(x86_fb_unbind(&owner));
        context.Info.Framebuffer.Format = format;
        CHECK(x86_fb_bind(&context, &window, &lapic, &inventory, &owner));
        for (channel = 0; channel < 256; ++channel) {
            unsigned char r = (unsigned char)channel, g = (unsigned char)(255 - channel), b = 17;
            BOOT_U32 color = format ? (BOOT_U32)b | ((BOOT_U32)g << 8) | ((BOOT_U32)r << 16) :
                (BOOT_U32)r | ((BOOT_U32)g << 8) | ((BOOT_U32)b << 16);
            CHECK(x86_fb_pixel(&owner, 0, 0, r, g, b));
            CHECK(words[0] == color && words[1] == 0xa5a5a5a5);
        }
        for (y = 0; y < 3; ++y) for (x = 0; x < 1024; ++x) {
            CHECK(x86_fb_pixel(&owner, x, y, 9, 8, 7));
            CHECK(words[y * 1024 + x] == (format ? 0x00090807U : 0x00070809U));
        }
        CHECK(words[3072] == 0xa5a5a5a5 && pool.FreePages == 125);
        CHECK(x86_fb_unbind(&owner) && pool.FreePages == 128);
    }
    return 0;
}

// ====== pixel_offsets ======

static int pixel_offsets(void)
{
    BOOT_U32 index;
    CHECK(setup()); CHECK(x86_fb_unbind(&owner));
    context.Info.Framebuffer.Base += 4092;
    context.Info.Framebuffer.Width = 7; context.Info.Framebuffer.Height = 3;
    context.Info.Framebuffer.Stride = 9; context.Info.Framebuffer.Used = 108;
    CHECK(x86_fb_bind(&context, &window, &lapic, &inventory, &owner));
    CHECK(x86_fb_pixel(&owner, 6, 2, 1, 2, 3));
    CHECK(words[1023 + 24] == 0x00030201U);
    for (index = 0; index < 4100; ++index)
        CHECK(index == 1047 || words[index] == 0xa5a5a5a5);
    CHECK(!x86_fb_pixel(&owner, 7, 0, 255, 255, 255));
    CHECK(!x86_fb_pixel(&owner, 0, 3, 255, 255, 255));
    CHECK(!x86_fb_pixel(&owner, ~0U, ~0U, 255, 255, 255));
    CHECK(visits == 1 && pool.FreePages == 125);
    CHECK(x86_fb_unbind(&owner) && pool.FreePages == 128);
    return 0;
}

// ====== pixel_rejection ======

static int pixel_rejection(void)
{
    X86_FB_MAPPING copy;
    BOOT_U32 index;
    BOOT_U64 entry;
    CHECK(setup()); copy = owner;
    CHECK(!x86_fb_pixel(&copy, 0, 0, 1, 2, 3) && !visits);
    owner.Ready = 3; CHECK(!x86_fb_pixel(&owner, 0, 0, 1, 2, 3)); owner.Ready = 2;
    bad_cpu = 1; CHECK(!x86_fb_pixel(&owner, 0, 0, 1, 2, 3)); bad_cpu = 0;
    cpu.Pat ^= 1; CHECK(!x86_fb_pixel(&owner, 0, 0, 1, 2, 3)); cpu.Pat ^= 1;
    window.Active = 2; CHECK(!x86_fb_pixel(&owner, 0, 0, 1, 2, 3)); window.Active = 0;
    frame(owner.Authority.Tables[2].Base)[1] |= 4;
    CHECK(!x86_fb_pixel(&owner, 0, 0, 1, 2, 3));
    frame(owner.Authority.Tables[2].Base)[1] &= ~4ULL;
    entry = frame(owner.Authority.Tables[2].Base)[1];
    for (index = 0; index < 3; ++index) {
        frame(owner.Authority.Tables[2].Base)[1] = entry ^
            (index == 0 ? 2ULL : index == 1 ? (1ULL << 63) : 0x18ULL);
        CHECK(!x86_fb_pixel(&owner, 0, 0, 1, 2, 3));
    }
    frame(owner.Authority.Tables[2].Base)[1] = entry;
    CHECK(kernel_physical_pin(&pool, &owner.Authority.Tables[2], 0));
    CHECK(!x86_fb_pixel(&owner, 0, 0, 1, 2, 3));
    CHECK(kernel_physical_pin(&pool, &owner.Authority.Tables[2], 1));
    context.Info.Framebuffer.Format = 2;
    CHECK(!x86_fb_pixel(&owner, 0, 0, 1, 2, 3)); context.Info.Framebuffer.Format = 0;
    CHECK(!visits && words[0] == 0xa5a5a5a5);
    fail_view = 1; CHECK(!x86_fb_pixel(&owner, 0, 0, 1, 2, 3) && visits == 1);
    CHECK(words[0] == 0xa5a5a5a5);
    CHECK(x86_fb_unbind(&owner));
    CHECK(!x86_fb_pixel(&owner, 0, 0, 1, 2, 3) && visits == 1);
    return 0;
}

// ====== pixel_target ======

static int target_check(const X86_FB_TARGET *target) { return target && target_valid; }

// Z1-A: while a target is registered the same GOP geometry is written at the target base; the
// GOP aperture stays untouched; refusals and the invalidated target leave nothing written.
static int pixel_target(void)
{
    X86_FB_TARGET target = {0, TARGET_BASE, sizeof(target_words), target_check}, other = target;
    CHECK(setup());
    memset(target_words, 0x5a, sizeof(target_words)); target_valid = 1;
    target.Self = (BOOT_U64)&target; other.Self = (BOOT_U64)&other;
    X86_FB_TARGET bad = target; bad.Base += 4; bad.Self = (BOOT_U64)&bad;
    CHECK(!x86_fb_target_redirect(0, &bad));
    bad = target; bad.Self = 0; CHECK(!x86_fb_target_redirect(0, &bad));
    bad = target; bad.Self = (BOOT_U64)&bad; bad.Bytes = 0; CHECK(!x86_fb_target_redirect(0, &bad));
    bad.Bytes = target.Bytes; bad.Valid = 0; CHECK(!x86_fb_target_redirect(0, &bad));
    target_valid = 0; CHECK(!x86_fb_target_redirect(0, &target)); target_valid = 1;
    CHECK(x86_fb_target_redirect(0, &target) && x86_fb_target() == &target);
    CHECK(!x86_fb_target_redirect(0, &other) && !x86_fb_target_redirect(&other, 0));
    CHECK(x86_fb_pixel(&owner, 5, 1, 1, 2, 3) && target_words[1024 + 5] != 0x5a5a5a5a && words[1024 + 5] == 0xa5a5a5a5);
    target_valid = 0;
    CHECK(!x86_fb_pixel(&owner, 6, 1, 1, 2, 3) && target_words[1024 + 6] == 0x5a5a5a5a && words[1024 + 6] == 0xa5a5a5a5);
    target_valid = 1; target.Bytes = 64; // smaller than the GOP layout: refused at begin
    CHECK(!x86_fb_pixel(&owner, 7, 1, 1, 2, 3) && target_words[1024 + 7] == 0x5a5a5a5a);
    target.Bytes = sizeof(target_words);
    CHECK(x86_fb_target_redirect(&target, 0) && !x86_fb_target());
    CHECK(x86_fb_pixel(&owner, 8, 1, 1, 2, 3) && words[1024 + 8] != 0xa5a5a5a5 && target_words[1024 + 8] == 0x5a5a5a5a);
    CHECK(x86_fb_unbind(&owner));
    return 0;
}

// ====== run_pixel_tests ======

int run_pixel_tests(void)
{
    int result = pixel_colors();
    if (result) return result;
    result = pixel_offsets();
    if (result) return result;
    result = pixel_target();
    return result ? result : pixel_rejection();
}

// ====== draw_setup ======

static int draw_setup(BOOT_U32 width, BOOT_U32 height, BOOT_U32 stride, BOOT_U32 format)
{
    if (!setup() || !x86_fb_unbind(&owner)) return 0;
    context.Info.Framebuffer.Width = width; context.Info.Framebuffer.Height = height;
    context.Info.Framebuffer.Stride = stride; context.Info.Framebuffer.Format = format;
    context.Info.Framebuffer.Used = (BOOT_U64)stride * height * 4;
    return x86_fb_bind(&context, &window, &lapic, &inventory, &owner);
}

// ====== rectangles ======

static int rectangles(void)
{
    BOOT_U32 format, index, single_scan;
    for (format = 0; format < 2; ++format) {
        CHECK(draw_setup(64, 64, 64, format));
        table_views = 0;
        CHECK(x86_fb_pixel(&owner, 0, 0, 1, 2, 3)); single_scan = table_views;
        table_views = visits = 0;
        CHECK(x86_fb_rectangle(&owner, 0, 0, 64, 64, 0x010203));
        CHECK(visits == 4096 && table_views == single_scan);
        for (index = 0; index < 4096; ++index)
            CHECK(words[index] == (format ? 0x00010203U : 0x00030201U));
        CHECK(words[4096] == 0xa5a5a5a5 && pool.FreePages == 125);
        CHECK(x86_fb_unbind(&owner) && pool.FreePages == 128);
    }
    CHECK(draw_setup(7, 3, 9, 0));
    CHECK(x86_fb_rectangle(&owner, 5, 1, 2, 2, 0xff0000));
    for (index = 0; index < 27; ++index)
        CHECK(words[index] == ((index == 14 || index == 15 || index == 23 || index == 24) ?
            0xffU : 0xa5a5a5a5));
    CHECK(x86_fb_unbind(&owner));
    return 0;
}

// ====== text_utf8 ======

static int text_utf8(void)
{
    const X86_FB_GLYPH glyphs[] = {
        {0x41, {0x80,0x40,0x20,0x10,0x08,0x04,0x02,0x01}},
        {0xe0, {0x10,0x08,0x00,0x3c,0x02,0x3e,0x42,0x3e}},
        {0xe8, {0x10,0x08,0x00,0x3c,0x42,0x7e,0x40,0x3c}},
        {0xe9, {0x08,0x10,0x00,0x3c,0x42,0x7e,0x40,0x3c}},
        {0xec, {0x20,0x10,0x00,0x30,0x10,0x10,0x10,0x38}},
        {0xf2, {0x10,0x08,0x00,0x3c,0x42,0x42,0x42,0x3c}},
        {0xf9, {0x10,0x08,0x00,0x42,0x42,0x42,0x46,0x3a}}};
    X86_FB_FONT font = {glyphs, 7, sizeof(glyphs), {0xff,0x81,0x81,0x81,0x81,0x81,0x81,0xff}, 8, 8};
    const unsigned char sample[] = "A\xc3\xa0\xc3\xa8\xc3\xa9\xc3\xac\xc3\xb2\xc3\xb9\xf0\x9f\x98\x80";
    X86_FB_TEXT text = {sample, sizeof(sample) - 1, sizeof(sample), 3, 2, 0x112233, 0x445566};
    BOOT_U32 format, cell, row, col, scan;
    for (format = 0; format < 2; ++format) {
        CHECK(draw_setup(80, 12, 84, format));
        table_views = 0; CHECK(x86_fb_pixel(&owner, 0, 0, 0, 0, 0)); scan = table_views;
        table_views = visits = 0;
        CHECK(x86_fb_text(&owner, &text, &font));
        CHECK(visits == 512 && table_views == scan);
        for (cell = 0; cell < 8; ++cell) for (row = 0; row < 8; ++row) for (col = 0; col < 8; ++col) {
            unsigned short bitmap = cell < 7 ? glyphs[cell].Rows[row] : font.Missing[row];
            BOOT_U32 rgb = (bitmap & (0x80U >> col)) ? text.Foreground : text.Background;
            BOOT_U32 color = format ? rgb : ((rgb & 255) << 16) | (rgb & 0xff00) | (rgb >> 16);
            CHECK(words[(row + 2) * 84 + cell * 8 + col + 3] == color);
        }
        CHECK(words[2 * 84 + 2] == 0xa5a5a5a5 && words[2 * 84 + 67] == 0xa5a5a5a5);
        CHECK(words[2 * 84 + 80] == 0xa5a5a5a5);
        visits = 0;
        CHECK(!x86_fb_text_caret(&owner, &text, &font, 0) && !visits);
        CHECK(x86_fb_unbind(&owner));
    }
    return 0;
}

// ====== drawing_rejections ======

static int drawing_rejections(void)
{
    X86_FB_GLYPH glyph = {65, {0xff,0,0,0,0,0,0,0}};
    X86_FB_FONT font = {&glyph, 1, sizeof(glyph), {0xff,0,0,0,0,0,0,0}, 8, 8};
    unsigned char sample[257];
    X86_FB_TEXT text = {sample, 64, sizeof(sample), 0, 0, 0xffffff, 0};
    BOOT_U32 index;
    X86_FB_PIXEL_SESSION session;
    X86_FB_GLYPH duplicate[2] = {{65, {0}}, {65, {0}}};
    CHECK(draw_setup(512, 8, 512, 1)); memset(sample, 'A', sizeof(sample));
    CHECK(x86_fb_text(&owner, &text, &font) && visits == 4096);
    for (index = 0; index < 64; ++index) {
        sample[index * 4] = 0xf0; sample[index * 4 + 1] = 0x9f;
        sample[index * 4 + 2] = 0x98; sample[index * 4 + 3] = 0x80;
    }
    text.Bytes = 256; visits = 0;
    CHECK(x86_fb_text(&owner, &text, &font) && visits == 4096);
    memset(sample, 'A', sizeof(sample));
    memset(words, 0xa5, sizeof(words)); visits = 0;
    text.Bytes = 65; CHECK(!x86_fb_text(&owner, &text, &font));
    text.Bytes = 257; CHECK(!x86_fb_text(&owner, &text, &font));
    text.Bytes = 1; text.Capacity = 0; CHECK(!x86_fb_text(&owner, &text, &font));
    text.Capacity = 257; text.X = 505; CHECK(!x86_fb_text(&owner, &text, &font)); text.X = 0;
    for (index = 0; index < 32; ++index) {
        sample[0] = (unsigned char)index; CHECK(!x86_fb_text(&owner, &text, &font));
    }
    sample[0] = 0xc3; CHECK(!x86_fb_text(&owner, &text, &font));
    sample[0] = 'A'; text.Foreground = 0xffffffff;
    CHECK(!x86_fb_text(&owner, &text, &font)); text.Foreground = 0xffffff;
    font.Bytes = sizeof(glyph) - 1; CHECK(!x86_fb_text(&owner, &text, &font)); font.Bytes = sizeof(glyph);
    font.Count = 129; CHECK(!x86_fb_text(&owner, &text, &font)); font.Count = 1;
    font.Missing[0] = 0; CHECK(!x86_fb_text(&owner, &text, &font)); font.Missing[0] = 0xff;
    glyph.Scalar = 0xd800; CHECK(!x86_fb_text(&owner, &text, &font)); glyph.Scalar = 65;
    font.Glyphs = duplicate; font.Count = 2; font.Bytes = sizeof(duplicate);
    CHECK(!x86_fb_text(&owner, &text, &font)); duplicate[1].Scalar = 64;
    CHECK(!x86_fb_text(&owner, &text, &font));
    font.Glyphs = &glyph; font.Count = 1; font.Bytes = sizeof(glyph);
    CHECK(!x86_fb_text(&owner, 0, &font) && !x86_fb_text(&owner, &text, 0));
    CHECK(!x86_fb_rectangle(&owner, 0, 0, 512, 9, 0));
    CHECK(!x86_fb_rectangle(&owner, 0, 0, 0, 8, 0));
    CHECK(!x86_fb_rectangle(&owner, ~0U, 0, 1, 1, 0));
    CHECK(!x86_fb_rectangle(&owner, 0, 0, 1, 1, 0xffffffff));
    bad_cpu = 1; CHECK(!x86_fb_text(&owner, &text, &font)); bad_cpu = 0;
    frame(owner.Authority.Tables[2].Base)[1] |= 4;
    CHECK(!x86_fb_rectangle(&owner, 0, 0, 1, 1, 0));
    CHECK(!x86_fb_text(&owner, &text, &font));
    frame(owner.Authority.Tables[2].Base)[1] &= ~4ULL;
    CHECK(!visits);
    for (index = 0; index < 4100; ++index) CHECK(words[index] == 0xa5a5a5a5);
    CHECK(x86_fb_pixels_begin(&owner, 2, 2, 2, 2, &session));
    CHECK(!x86_fb_pixels_store(&session, 1, 2, 0));
    CHECK(!x86_fb_pixels_store(&session, 2, 1, 0));
    CHECK(!x86_fb_pixels_store(&session, 4, 2, 0));
    CHECK(!x86_fb_pixels_store(&session, 2, 4, 0));
    CHECK(!visits && x86_fb_pixels_end(&session));
    fail_after = 2;
    CHECK(!x86_fb_rectangle(&owner, 0, 0, 3, 1, 0xffffff));
    CHECK(words[0] == 0xffffff && words[1] == 0xa5a5a5a5 && visits == 2);
    fail_after = 0; visits = 0; corrupt_after = 1;
    CHECK(!x86_fb_rectangle(&owner, 0, 0, 1, 1, 0x010203));
    CHECK(words[0] == 0x010203); cpu.Pat ^= 1;
    CHECK(x86_fb_unbind(&owner) && pool.FreePages == 128);
    CHECK(draw_setup(7, 9, 9, 0)); CHECK(x86_fb_unbind(&owner));
    context.Info.Framebuffer.Base += 4092;
    CHECK(x86_fb_bind(&context, &window, &lapic, &inventory, &owner));
    CHECK(x86_fb_rectangle(&owner, 6, 8, 1, 1, 0x112233));
    CHECK(words[1023 + 8 * 9 + 6] == 0x00332211);
    CHECK(words[1023 + 8 * 9 + 7] == 0xa5a5a5a5);
    CHECK(x86_fb_unbind(&owner));
    return 0;
}

// ====== caret_pixels ======

static int caret_pixels(void)
{
    const X86_FB_FONT *font = x86_fb_console_font();
    const BOOT_U32 w = font->Width, h = font->Height, stride = 2 * w + 4;
    const unsigned char sample[] = "AA";
    X86_FB_TEXT text = {sample, 2, sizeof(sample), 0, 0, 0x112233, 0x445566};
    CHECK(w == 9 && h == 18);
    for (BOOT_U32 format = 0; format < 2; ++format) {
        CHECK(draw_setup(2 * w, h, stride, format));
        visits = 0;
        CHECK(x86_fb_text_caret(&owner, &text, font, 1) && visits == 2 * w * h);
        for (BOOT_U32 row = 0; row < h; ++row) for (BOOT_U32 col = 0; col < 2 * w; ++col) {
            BOOT_U32 rgb = x86_fb_text_color(row >= h-2 && col >= w ? ~0ULL : x86_fb_font_row(font,65-32,row),col%w,0x112233,0x445566);
            BOOT_U32 color = format ? rgb : ((rgb & 255) << 16) | (rgb & 0xff00) | (rgb >> 16);
            CHECK(words[row * stride + col] == color);
            CHECK(words[row * stride + 2 * w] == 0xa5a5a5a5);
        }
        visits = 0;
        CHECK(!x86_fb_text_caret(&owner, &text, font, 2) && !visits);
        CHECK(x86_fb_text_caret(&owner, &text, font, 0) && visits == 2 * w * h);
        for (BOOT_U32 row = h - 2; row < h; ++row) for (BOOT_U32 col = w; col < 2 * w; ++col) {
            BOOT_U32 rgb = x86_fb_text_color(x86_fb_font_row(font,65-32,row),col%w,0x112233,0x445566);
            CHECK(words[row * stride + col] == (format ? rgb : ((rgb & 255) << 16) | (rgb & 0xff00) | (rgb >> 16)));
        }
        CHECK(x86_fb_unbind(&owner) && pool.FreePages == 128);
    }
    return 0;
}

// ====== grayscale_boundaries ======

static int grayscale_boundaries(void)
{
    const X86_FB_FONT *font = x86_fb_console_font();
    CHECK(!x86_fb_font_row(NULL,0,0) && !x86_fb_font_row(font,0,18));
    CHECK(x86_fb_text_color(0,0,0xffffff,0x123456) == 0x123456);
    CHECK(x86_fb_text_color(15,0,0xabcdef,0) == 0xabcdef);
    CHECK(x86_fb_text_color(8,0,0xffffff,0) == 0x888888);
    CHECK(x86_fb_text_color(7ULL << 32,8,0xffffff,0) == 0x777777);
    CHECK(x86_fb_text_color(~0ULL,9,0,0x123456) == 0x123456);
    for (BOOT_U32 a = 0; a < 16; ++a) {
        BOOT_U32 expected = ((17*a+68*(15-a)+7)/15) << 16 |
            ((34*a+85*(15-a)+7)/15) << 8 | (51*a+102*(15-a)+7)/15;
        CHECK(x86_fb_text_color(a,0,0x112233,0x445566) == expected);
    }
    return 0;
}

// ====== run_drawing_tests ======

static int console_font(void)
{
    const X86_FB_FONT *font = x86_fb_console_font();
    const BOOT_U32 w = font->Width, h = font->Height, cells = 25;
    unsigned char sample[132];
    X86_FB_TEXT text = {sample, cells, sizeof(sample), 0, 0, 0xffffff, 0};
    BOOT_U32 index, row, col;
    CHECK(h == 18 && w == 9 && font->Count == 109);
    for (index = 0; index < font->Count; ++index) {
        for (row = 0; row < h; ++row) CHECK(font->Glyphs[index].Rows[row] >> w == 0);
    }
    for (row = 0; row < h; ++row) CHECK(font->Missing[row] >> w == 0);
    CHECK(draw_setup(cells * w, h, cells * w, 1)); memset(sample, 'A', sizeof(sample));
    CHECK(x86_fb_text(&owner, &text, font) && visits == cells * w * h);
    for (row = 0; row < h; ++row) for (col = 0; col < cells * w; ++col)
        CHECK(words[row * cells * w + col] == x86_fb_text_color(x86_fb_font_row(font,65-32,row),col%w,0xffffff,0));
    visits = 0;
    CHECK(x86_fb_text_caret(&owner, &text, font, cells - 1) && visits == cells * w * h);
    for (row = 0; row < h; ++row) for (col = 0; col < cells * w; ++col)
        CHECK(words[row * cells * w + col] == x86_fb_text_color(row >= h-2 && col >= (cells-1)*w ? ~0ULL : x86_fb_font_row(font,65-32,row),col%w,0xffffff,0));
    visits = 0;
    CHECK(!x86_fb_text_caret(&owner, &text, font, cells) && !visits);
    CHECK(x86_fb_text(&owner, &text, font) && visits == cells * w * h);
    for (row = h - 2; row < h; ++row) for (col = (cells - 1) * w; col < cells * w; ++col)
        CHECK(words[row * cells * w + col] == x86_fb_text_color(x86_fb_font_row(font,65-32,row),col%w,0xffffff,0));
    text.Bytes = cells + 1; visits = 0; CHECK(!x86_fb_text(&owner, &text, font) && !visits);
    {
        X86_FB_FONT bad = *font;
        text.Bytes = 1;
        for (index = 0; index < 3; ++index) {
            bad.Height = index == 0 ? 0 : index == 1 ? 17 : ~0U;
            CHECK(!x86_fb_text(&owner, &text, &bad) && !visits);
        }
        bad = *font;
        for (index = 0; index < 4; ++index) {
            bad.Width = index == 0 ? 0 : index == 1 ? 10 : index == 2 ? 16 : ~0U;
            CHECK(!x86_fb_text(&owner, &text, &bad) && !visits);
        }
    }
    CHECK(x86_fb_unbind(&owner) && pool.FreePages == 128);
    return 0;
}

// ====== run_drawing_tests ======

int run_drawing_tests(void)
{
    BOOT_U32 iteration;
    for (iteration = 0; iteration < 100; ++iteration) {
        int result = grayscale_boundaries();
        if (!result) result = rectangles();
        if (!result) result = text_utf8();
        if (!result) result = drawing_rejections();
        if (!result) result = console_font();
        if (!result) result = caret_pixels();
        if (!result) result = run_pointer_drawing_tests(draw_setup, &owner, words);
        if (result) return result;
    }
    return 0;
}
