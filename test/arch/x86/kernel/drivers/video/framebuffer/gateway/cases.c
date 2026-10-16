#include "../../../../../../../../arch/x86/kernel/drivers/video/framebuffer/gateway/cell.h"
#include "../../../../../../../../arch/x86/kernel/drivers/video/framebuffer/drawing/draw.h"
#include <stdio.h>
#include <stdlib.h>
static KERNEL_BOOT_CONTEXT context;
static X86_FB_MAPPING owner;
static BOOT_U64 flags, counter;
static BOOT_U32 calls, switches, supported = 1, failure;
static BOOT_U32 expected_scalar;
static BOOT_U32 group_count, group_scalars[8];
static const BOOT_U32 group_values[4] = {'A', 0xe8, 0x20ac, 0x1f600};
#define CHECK(value) do { if (!(value)) { \
    fprintf(stderr, "FAIL cell gateway line %d\n", __LINE__); exit(1); } } while (0)

// ====== clock_and_irq_mocks ======

int x86_fb_cell_clock_available(void) { return (int)supported; }
BOOT_U64 x86_fb_cell_stamp(void) { counter += 100; return counter; }
BOOT_U64 x86_fb_cell_suspend(void)
{
    BOOT_U64 saved = flags;
    flags &= ~512ULL;
    ++switches;
    return saved;
}
int x86_fb_cell_resume(BOOT_U64 saved) { flags = (flags & ~512ULL) | (saved & 512); return 1; }

// ====== x86_fb_text ======

int x86_fb_text(X86_FB_MAPPING *mapping, const X86_FB_TEXT *text, const X86_FB_FONT *font)
{
    BOOT_U32 offset = 0, glyph = 0;
    BOOT_U64 scalar;
    CHECK(mapping == &owner && !(flags & 512) && text->Capacity == (group_count ? 32U : 4U) && font->Height == 20 && font->Width == 10);
    CHECK(text->Bytes >= 1 && text->Bytes <= text->Capacity);
    while (offset < text->Bytes) {
        BOOT_U32 packed = 0, byte;
        for (byte = 0; byte < 4 && byte < text->Bytes - offset; ++byte)
            packed |= (BOOT_U32)text->Text[offset + byte] << (byte * 8);
        scalar = x86_fb_utf8_scalar(packed, text->Bytes - offset);
        CHECK(scalar != ~0ULL && glyph < (group_count ? group_count : 1U));
        CHECK((BOOT_U32)scalar == (group_count ? group_scalars[glyph] : expected_scalar));
        offset += (BOOT_U32)(scalar >> 32); ++glyph;
    }
    CHECK(glyph == (group_count ? group_count : 1U));
    CHECK(text->X % 10 == 0 && text->Y % 20 == 0);
    ++calls;
    return !failure;
}

// ====== main ======

int main(void)
{
    BOOT_U64 ticks;
    BOOT_U32 scalar, initial, count = 0;
    owner.Self = (BOOT_U64)&owner; owner.Ready = 2; owner.Context = &context;
    context.Info.Framebuffer.Base = 4096;
    context.Info.Framebuffer.Width = 1920; context.Info.Framebuffer.Height = 1080;
    for (initial = 0; initial < 2; ++initial) {
        flags = 2 | ((BOOT_U64)initial << 9);
        for (scalar = 0; scalar <= 0x110000; ++scalar) {
            int expected = (int)x86_fb_font_scalar(scalar);
            expected_scalar = scalar;
            ticks = 7;
            CHECK(x86_fb_cell(&owner, 191, 53, scalar, 0xffffff, 0, &ticks) == expected);
            CHECK(flags == (2 | ((BOOT_U64)initial << 9)));
            CHECK(ticks == (expected ? 100U : 7U));
            ++count;
        }
        expected_scalar = 'A'; failure = 1; ticks = 7;
        CHECK(!x86_fb_cell(&owner, 0, 0, 'A', 0, 0, &ticks) && ticks == 100);
        CHECK(flags == (2 | ((BOOT_U64)initial << 9)));
        failure = 0;
        supported = 0; ticks = 7;
        CHECK(!x86_fb_cell(&owner, 0, 0, 'A', 0, 0, &ticks) && ticks == 7);
        supported = 1;
        CHECK(!x86_fb_cell(&owner, 192, 0, 'A', 0, 0, &ticks) && !x86_fb_cell(&owner, 240, 0, 'A', 0, 0, &ticks));
        CHECK(!x86_fb_cell(&owner, 0, 54, 'A', 0, 0, &ticks) && !x86_fb_cell(&owner, 0, 67, 'A', 0, 0, &ticks));
        CHECK(!x86_fb_cell(&owner, 0, 0, 'A', UINT32_MAX, 0, &ticks));
        CHECK(!x86_fb_cell(&owner, 0, 0, 'A', 0, 0, NULL));
        CHECK(!x86_fb_cell(NULL, 0, 0, 'A', 0, 0, &ticks));
    }
    CHECK(switches == calls);
    group_count = 8;
    for (scalar = 0; scalar < 8; ++scalar) group_scalars[scalar] = group_values[scalar % 4];
    for (initial = 0; initial < 2; ++initial) {
        flags = 2 | ((BOOT_U64)initial << 9);
        ticks = 7;
        CHECK(x86_fb_group(&owner, 184, 53, group_scalars, 8, 0xffffff, 0, &ticks) && ticks == 100);
        failure = 1;
        CHECK(!x86_fb_group(&owner, 0, 0, group_scalars, 8, 0, 0, &ticks)); failure = 0;
        CHECK(flags == (2 | ((BOOT_U64)initial << 9)));
        ticks = 7;
        CHECK(!x86_fb_group(&owner, 185, 53, group_scalars, 8, 0, 0, &ticks));
        CHECK(!x86_fb_group(&owner, 0, 0, group_scalars, 0, 0, 0, &ticks));
        CHECK(!x86_fb_group(&owner, 0, 0, group_scalars, 9, 0, 0, &ticks));
        CHECK(!x86_fb_group(&owner, 0, 0, NULL, 8, 0, 0, &ticks));
        CHECK(!x86_fb_group(&owner, 0, 0, (const BOOT_U32 *)(UINT64_MAX - 3), 8, 0, 0, &ticks));
        group_scalars[7] = 0xd800;
        CHECK(!x86_fb_group(&owner, 0, 0, group_scalars, 8, 0, 0, &ticks) && ticks == 7);
        group_scalars[7] = 0x1f600;
    }
    CHECK(switches == calls);
    for (scalar = 0; scalar < 8; ++scalar) group_scalars[scalar] = 0x1f600;
    CHECK(x86_fb_group(&owner, 184, 53, group_scalars, 8, 0, 0, &ticks));
    group_count = 1;
    CHECK(x86_fb_group(&owner, 191, 53, group_scalars, 1, 0, 0, &ticks));
    CHECK(x86_fb_group_caret(&owner, 191, 53, group_scalars, 1, 0, 0, &ticks, 0));
    scalar = calls;
    CHECK(!x86_fb_group_caret(&owner, 191, 53, group_scalars, 1, 0, 0, &ticks, 1));
    CHECK(calls == scalar);
    CHECK(!x86_fb_group(&owner, 0, 54, group_scalars, 1, 0, 0, &ticks));
    CHECK(!x86_fb_group(&owner, 0, 0, group_scalars, 1, UINT32_MAX, 0, &ticks));
    CHECK(!x86_fb_group(&owner, 0, 0, group_scalars, 1, 0, 0, NULL));
    CHECK(switches == calls);
    printf("PASS gateway: %u scalar cases; IF0/1 restored, failed draw retained, no allocations\n", count);
    return 0;
}

// ====== x86_fb_text_caret ======

int x86_fb_text_caret(X86_FB_MAPPING *mapping, const X86_FB_TEXT *text,
    const X86_FB_FONT *font, BOOT_U32 caret)
{
    CHECK(caret == ~0U || caret < group_count);
    return x86_fb_text(mapping, text, font);
}

// ====== x86_fb_text_pointer ======

int x86_fb_text_pointer(X86_FB_MAPPING *mapping, const X86_FB_TEXT *text,
    const X86_FB_FONT *font, BOOT_U32 caret, const KERNEL_POINTER_IMAGE *pointer)
{
    CHECK(pointer != NULL);
    return x86_fb_text_caret(mapping, text, font, caret);
}
