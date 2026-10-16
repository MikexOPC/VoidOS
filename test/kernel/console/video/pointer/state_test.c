#include "kernel/console/video/pointer/state.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static KERNEL_VIDEO_CONSOLE view;
static KERNEL_CONSOLE_POINTER owner;
static uint32_t checks;
#define CHECK(value) do { ++checks; if (!(value)) { fprintf(stderr,"FAIL pointer line %d\n",__LINE__); exit(1); } } while (0)

// ====== clean_dirty ======

static void clean_dirty(void)
{
    uint32_t index, scalar;
    while (kernel_console_peek(&view, 0, &index, &scalar) == 1) CHECK(kernel_console_ack(&view, index, scalar));
}

// ====== dirty_count ======

static uint32_t dirty_count(void)
{
    uint32_t count = 0;
    for (uint32_t index = 0; index < view.Columns * view.Rows; ++index)
        count += (view.Dirty[index / 8] >> (index % 8)) & 1;
    return count;
}

// ====== main ======

int main(void)
{
    uint32_t black = 0, white = 0, transparent = 0;
    for (uint32_t y = 0; y < 18; ++y) for (uint32_t x = 0; x < 18; ++x) {
        uint32_t sample = kernel_pointer_sample(x, y);
        CHECK((sample & 31) + (sample >> 5) <= 16);
        black += sample != 0 && sample != 512; white += sample == 512; transparent += sample == 0;
    }
    CHECK(black && white && transparent && black + white + transparent == 324);
    CHECK(!kernel_pointer_sample(UINT32_MAX, 1) && !kernel_pointer_sample(1, UINT32_MAX));
    CHECK(kernel_pointer_sample(KERNEL_POINTER_HOTSPOT_X, KERNEL_POINTER_HOTSPOT_Y) != 0);
    CHECK(kernel_pointer_blend(512, 0x123456) == 0xffffff);
    CHECK(kernel_pointer_blend(16, 0x123456) == 0);
    CHECK(kernel_pointer_blend(8, 0x204060) == 0x102030);
    CHECK(kernel_pointer_blend(256, 0) == 0x808080);
    CHECK(kernel_pointer_blend(0, 0x123456) == 0x123456);
    CHECK(!kernel_pointer_init(NULL, &view));
    CHECK(kernel_console_init(&view, 113, 42)); clean_dirty(); // 1024x768 in 9x18 cells: 1017x756 usable pixels
    CHECK(kernel_pointer_init(&owner, &view));
    CHECK(dirty_count() <= 12 && dirty_count() != 0); clean_dirty();
    CHECK(!kernel_pointer_init(&owner, &view));
    {
        KERNEL_CONSOLE_POINTER copy = owner;
        CHECK(!kernel_pointer_move(&copy, 1, 1, 0));
    }
    for (uint32_t index = 0; index < 1000; ++index) {
        int32_t dx = (int32_t)(index % 512) - 256;
        int32_t dy = (int32_t)((index * 37) % 512) - 256;
        uint64_t expected = kernel_pointer_plan(1017, 756, owner.Image.X, owner.Image.Y, dx, dy);
        CHECK(kernel_pointer_move(&owner, dx, dy, index % 8));
        CHECK(owner.Image.X == (uint32_t)expected && owner.Image.Y == expected >> 32);
        CHECK(dirty_count() <= 12); // two 18x18 images touch at most 3x2 cells each
        clean_dirty();
        CHECK(!kernel_pointer_move(&owner, 256, 0, 0));
        CHECK(!kernel_pointer_move(&owner, 0, INT32_MIN, 0));
        CHECK(!kernel_pointer_move(&owner, 0, 0, 8));
        CHECK(dirty_count() == 0);
    }
    CHECK(kernel_pointer_move(&owner, 255, 255, 0));
    clean_dirty();
    CHECK(kernel_pointer_request(&owner, -1, 1, 3));
    CHECK(dirty_count() == 0 && owner.Buttons == 3);
    CHECK(kernel_pointer_move(&owner, -256, -256, 0));
    CHECK(dirty_count() <= 12); clean_dirty();
    CHECK(!kernel_pointer_close(&owner));
    CHECK(kernel_pointer_hide(&owner));
    CHECK(!kernel_pointer_close(&owner));
    CHECK(!kernel_pointer_move(&owner, 1, 1, 0));
    clean_dirty();
    CHECK(kernel_pointer_close(&owner));
    CHECK(!kernel_pointer_valid(&owner) && !kernel_pointer_hide(&owner));
    printf("PASS pointer: %u checks; fixed storage, bitmap and dirty rollback\n", checks);
    return 0;
}
