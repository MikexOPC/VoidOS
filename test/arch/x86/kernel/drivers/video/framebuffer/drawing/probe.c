#include "probe.h"
#include "../../../../../../../../arch/x86/kernel/drivers/video/framebuffer/drawing/draw.h"
#include "../../../../../../../../arch/x86/kernel/drivers/video/framebuffer/font/font.h"
#include "kernel/diagnostics/log.h"
#ifdef KERNEL_TEST_FRAMEBUFFER_MAPPING
static const X86_FB_GLYPH glyphs[] = {
    {0x20, {0,0,0,0,0,0,0,0}},
    {0x3a, {0,0x18,0x18,0,0x18,0x18,0,0}},
    {0x4f, {0x3c,0x42,0x42,0x42,0x42,0x42,0x3c,0}},
    {0x53, {0x3c,0x42,0x40,0x3c,0x02,0x42,0x3c,0}},
    {0xe0, {0x10,0x08,0x00,0x3c,0x02,0x3e,0x42,0x3e}},
    {0xe8, {0x10,0x08,0x00,0x3c,0x42,0x7e,0x40,0x3c}},
    {0xe9, {0x08,0x10,0x00,0x3c,0x42,0x7e,0x40,0x3c}},
    {0xec, {0x20,0x10,0x00,0x30,0x10,0x10,0x10,0x38}},
    {0xf2, {0x10,0x08,0x00,0x3c,0x42,0x42,0x42,0x3c}},
    {0xf9, {0x10,0x08,0x00,0x42,0x42,0x42,0x46,0x3a}}};
static const X86_FB_FONT font = {glyphs, 10, sizeof(glyphs),
    {0xff,0x81,0x81,0x81,0x81,0x81,0x81,0xff}, 8};
static const unsigned char sample[] = "OS: \xc3\xa0\xc3\xa8\xc3\xa9\xc3\xac\xc3\xb2\xc3\xb9\xe2\x82\xac";
static const unsigned char console[] = "MikeSW OS: \xc3\xa0\xc3\xa8\xc3\xa9\xc3\xac\xc3\xb2\xc3\xb9 \xe2\x82\xac";

// ====== font_probe ======

static int font_probe(X86_FB_MAPPING *owner)
{
    unsigned char ascii[95];
    BOOT_U32 index;
    X86_FB_TEXT text = {console, sizeof(console) - 1, sizeof(console), 16, 64, 0xffffff, 0x081018};
    if (!x86_fb_text(owner, &text, x86_fb_console_font())) return 0;
    for (index = 0; index < 95; ++index) ascii[index] = (unsigned char)(32 + index);
    text.Text = ascii; text.Capacity = sizeof(ascii); text.Y = 80;
    for (index = 0; index < 95; index += 32) {
        text.Text = ascii + index; text.Bytes = index < 64 ? 32 : 31;
        text.Capacity = 95 - index;
        if (!x86_fb_text(owner, &text, x86_fb_console_font())) return 0;
        text.Y += 16;
    }
    return 1;
}

// ====== x86_fb_drawing_probe ======

int x86_fb_drawing_probe(X86_FB_MAPPING *owner)
{
    X86_FB_TEXT text = {sample, sizeof(sample) - 1, sizeof(sample), 16, 40, 0xffffff, 0x081018};
    BOOT_U64 budget = owner->Window->Pool->FreePages;
    if (!x86_fb_rectangle(owner, 16, 16, 64, 16, 0x204060) ||
        !x86_fb_rectangle(owner, 80, 16, 32, 16, 0xd09030) ||
        !x86_fb_text(owner, &text, &font) ||
        !font_probe(owner) ||
        x86_fb_rectangle(owner, 0, 0, 65, 64, 0xffffff) ||
        owner->Window->Pool->FreePages != budget) return 0;
    return kernel_log(0, "KERNEL_FB_DRAWING_OK");
}
#endif
