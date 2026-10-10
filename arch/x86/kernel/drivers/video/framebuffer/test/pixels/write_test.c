#include "../lifecycle/fixture.h"
#include "../../pixels/write.h"
#include <string.h>
static BOOT_U32 words[4100], visits, fail_view;
#define CHECK(expression) do { if (!(expression)) return __LINE__; } while (0)

// ====== x86_fb_pixel_view ======

volatile BOOT_U32 *x86_fb_pixel_view(BOOT_U64 address)
{
    BOOT_U64 offset;
    ++visits;
    if (fail_view || address < X86_FB_VIRTUAL || (address & 3)) return 0;
    offset = (address - X86_FB_VIRTUAL) / 4;
    return offset < 4100 ? &words[offset] : 0;
}

// ====== setup ======

static int setup(void)
{
    visits = fail_view = 0;
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

// ====== run_pixel_tests ======

int run_pixel_tests(void)
{
    int result = pixel_colors();
    if (result) return result;
    result = pixel_offsets();
    return result ? result : pixel_rejection();
}
