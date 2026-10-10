#include "../../gateway/cell.h"
#include "../../drawing/draw.h"
#include <stdio.h>
#include <stdlib.h>
static KERNEL_BOOT_CONTEXT context;
static X86_FB_MAPPING owner;
static BOOT_U64 flags, counter;
static BOOT_U32 calls, switches, supported = 1, failure;
static BOOT_U32 expected_scalar;
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
    BOOT_U32 packed = 0, index;
    BOOT_U64 scalar;
    CHECK(mapping == &owner && !(flags & 512) && text->Capacity == 4 && font->Height == 16);
    CHECK(text->Bytes >= 1 && text->Bytes <= 4);
    for (index = 0; index < text->Bytes; ++index) packed |= (BOOT_U32)text->Text[index] << (index * 8);
    scalar = x86_fb_utf8_scalar(packed, text->Bytes);
    CHECK(scalar != ~0ULL && (scalar >> 32) == text->Bytes);
    CHECK((BOOT_U32)scalar == expected_scalar);
    CHECK(text->X % 8 == 0 && text->Y % 16 == 0);
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
            CHECK(x86_fb_cell(&owner, 239, 66, scalar, 0xffffff, 0, &ticks) == expected);
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
        CHECK(!x86_fb_cell(&owner, 240, 0, 'A', 0, 0, &ticks));
        CHECK(!x86_fb_cell(&owner, 0, 67, 'A', 0, 0, &ticks));
        CHECK(!x86_fb_cell(&owner, 0, 0, 'A', UINT32_MAX, 0, &ticks));
        CHECK(!x86_fb_cell(&owner, 0, 0, 'A', 0, 0, NULL));
        CHECK(!x86_fb_cell(NULL, 0, 0, 'A', 0, 0, &ticks));
    }
    CHECK(switches == calls);
    printf("PASS gateway: %u scalar cases; IF0/1 restored, failed draw retained, no allocations\n", count);
    return 0;
}
