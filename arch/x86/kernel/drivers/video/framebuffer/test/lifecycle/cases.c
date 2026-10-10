#include "fixture.h"
#include <stdio.h>
#include <string.h>
#define CHECK(expression) do { if (!(expression)) return __LINE__; } while (0)
int run_pixel_tests(void);

// ====== bind ======

static int bind(void)
{
    return x86_fb_bind(&context, &window, &lapic, &inventory, &owner);
}

// ====== roundtrip ======

static int roundtrip(void)
{
    BOOT_U32 index;
    X86_PAGING_INVENTORY stale;
    CHECK(fixture());
    CHECK(bind());
    CHECK(owner.Ready == 2 && owner.Allocated == 3);
    CHECK(pool.FreePages == 125 && borrows == 3 && flushes == 1);
    CHECK(x86_paging_inventory_verify_video(&context, &pool, &lapic, &owner.Authority,
        x86_fb_authority, &inventory));
    for (index = 0; index < owner.Allocated; ++index)
        CHECK(kernel_physical_is_pinned(&pool, &owner.Authority.Tables[index]));
    CHECK(frame(owner.Authority.Tables[2].Base)[0] == 0);
    CHECK(frame(owner.Authority.Tables[2].Base)[4] == 0);
    stale = inventory;
    CHECK(x86_fb_unbind(&owner)); CHECK(pool.FreePages == 128 && !owner.Self && !frame(4096)[225]);
    CHECK(x86_paging_inventory_verify_lapic(&context, &pool, &lapic, &inventory));
    CHECK(bind());
    CHECK(!x86_paging_inventory_verify_video(&context, &pool, &lapic, &owner.Authority,
        x86_fb_authority, &stale));
    CHECK(x86_fb_unbind(&owner)); CHECK(pool.FreePages == 128);
    return 0;
}

// ====== construction_failure ======

static int construction_failure(void)
{
    CHECK(fixture()); fail_borrow = 2; CHECK(!bind());
    CHECK(pool.FreePages == 128 && !owner.Self && !frame(4096)[225]);
    CHECK(fixture()); lose_view = 1; CHECK(!bind());
    CHECK(owner.Ready == 1 && pool.FreePages == 125 && window.Active == 2);
    CHECK(!x86_fb_unbind(&owner) && pool.FreePages == 125);
    repair_view(); CHECK(x86_fb_unbind(&owner)); CHECK(pool.FreePages == 128);
    return 0;
}

// ====== flush_failure ======

static int flush_failure(void)
{
    CHECK(fixture()); fail_flush = 2; CHECK(!bind());
    CHECK(pool.FreePages == 128 && !owner.Self && flushes == 2);
    CHECK(fixture()); fail_flush = 1; CHECK(!bind());
    CHECK(owner.Ready == 3 && owner.Published && pool.FreePages == 125);
    CHECK(!frame(4096)[225]); CHECK(!x86_fb_unbind(&owner) && pool.FreePages == 125);
    fail_flush = 0; CHECK(x86_fb_unbind(&owner)); CHECK(pool.FreePages == 128);
    return 0;
}

// ====== authority_changes ======

static int authority_changes(void)
{
    X86_FB_MAPPING copy;
    BOOT_U64 entry;
    CHECK(fixture()); CHECK(bind()); copy = owner;
    CHECK(!x86_fb_unbind(&copy) && pool.FreePages == 125);
    cpu.Pat ^= 1; CHECK(!x86_fb_unbind(&owner) && pool.FreePages == 125); cpu.Pat ^= 1;
    bad_cpu = 1; CHECK(!x86_fb_unbind(&owner)); bad_cpu = 0;
    entry = frame(4096)[225]; frame(4096)[225] = 0x2003;
    CHECK(!x86_fb_unbind(&owner) && frame(4096)[225] == 0x2003);
    frame(4096)[225] = entry;
    frame(owner.Authority.Tables[2].Base)[1] |= 4;
    CHECK(!x86_paging_inventory_verify_video(&context, &pool, &lapic, &owner.Authority,
        x86_fb_authority, &inventory));
    CHECK(x86_fb_unbind(&owner)); CHECK(pool.FreePages == 128);
    return 0;
}

// ====== absence_and_rejection ======

static int absence_and_rejection(void)
{
    CHECK(fixture()); memset(&context.Info.Framebuffer, 0, sizeof(context.Info.Framebuffer));
    CHECK(bind() && !owner.Self && !borrows && !flushes && pool.FreePages == 128);
    CHECK(fixture()); cpu.Pat = 0x0606060606060606ULL; CHECK(!bind() && !owner.Self);
    CHECK(fixture()); context.Info.Framebuffer.Base = 0x100000; CHECK(!bind() && !owner.Self);
    CHECK(fixture()); owner.Ready = 9; CHECK(!bind() && pool.FreePages == 128);
    CHECK(fixture()); frame(16384)[1] = 0x8000000000100003ULL;
    CHECK(x86_paging_inventory_capture_lapic(&context, &pool, &lapic, &inventory));
    CHECK(!bind() && !borrows && pool.FreePages == 128 && !owner.Self);
    return 0;
}

// ====== maximum_geometry ======

static int maximum_geometry(void)
{
    CHECK(fixture()); context.Info.Framebuffer.Base = 0xc0000ffc;
    context.Info.Framebuffer.Width = 1; context.Info.Framebuffer.Height = 1080;
    context.Info.Framebuffer.Stride = 4096; context.Info.Framebuffer.Used = 17694720;
    context.Info.Framebuffer.Capacity = 17694720;
    CHECK(bind()); CHECK(owner.Authority.Region.Pages == 4321 && owner.Allocated == 11);
    CHECK(pool.FreePages == 117);
    CHECK(frame(owner.Authority.Tables[10].Base)[226] == 0);
    CHECK(x86_fb_unbind(&owner)); CHECK(pool.FreePages == 128);
    return 0;
}

// ====== allocation_exhaustion ======

static int allocation_exhaustion(void)
{
    KERNEL_PHYSICAL_LEASE leases[62] = {0};
    BOOT_U32 index;
    CHECK(fixture());
    for (index = 0; index < 62; ++index) CHECK(kernel_physical_allocate(&pool, 1, &leases[index]));
    CHECK(pool.FreePages == 66);
    CHECK(!bind() && !owner.Self && pool.FreePages == 66 && !borrows && !frame(4096)[225]);
    for (index = 0; index < 62; ++index) CHECK(kernel_physical_release(&pool, &leases[index]));
    CHECK(pool.FreePages == 128);
    return 0;
}

// ====== main ======

int main(void)
{
    BOOT_U32 iteration, group;
    int (*cases[])(void) = {roundtrip, construction_failure, flush_failure, authority_changes,
        absence_and_rejection, maximum_geometry, allocation_exhaustion};
    for (iteration = 0; iteration < 100; ++iteration) for (group = 0; group < 7; ++group) {
        int result = cases[group]();
        if (result) { printf("FAIL: group %u iteration %u line %d\n", group, iteration, result); return 1; }
    }
    puts("PASS: 7 lifecycle groups x100; failure retention and complete budget restoration");
    {
        int result = run_pixel_tests();
        if (result) { printf("FAIL: pixel line %d\n", result); return 1; }
    }
    puts("PASS: 3 pixel groups; RGB/BGR, all pixels, offset/padding, no-write rejections");
    return 0;
}
