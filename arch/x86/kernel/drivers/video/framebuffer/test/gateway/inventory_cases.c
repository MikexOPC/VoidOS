#include "../../gateway/inventory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static KERNEL_BOOT_CONTEXT context, other_context;
static KERNEL_PHYSICAL_ALLOCATOR pool;
static X86_LAPIC_MAPPING lapic;
static X86_PAGING_INVENTORY inventory;
static X86_PAGE_WINDOW window;
static X86_FB_MAPPING owner;
static BOOT_U32 video_calls, lapic_calls, valid = 1, checks;
#define CHECK(value) do { ++checks; if (!(value)) { \
    fprintf(stderr, "FAIL inventory gateway line %d\n", __LINE__); exit(1); } } while (0)

// ====== inventory_mocks ======

int x86_fb_authority(const X86_PAGING_VIDEO_AUTH *authority)
{
    return authority == &owner.Authority && valid;
}

int x86_paging_inventory_verify_video(const KERNEL_BOOT_CONTEXT *ctx,
    const KERNEL_PHYSICAL_ALLOCATOR *physical, const X86_LAPIC_MAPPING *mapping,
    const X86_PAGING_VIDEO_AUTH *authority, X86_PAGING_VIDEO_CHECK check,
    const X86_PAGING_INVENTORY *snapshot)
{
    CHECK(ctx == &context && physical == &pool && mapping == &lapic && snapshot == &inventory);
    CHECK(authority == &owner.Authority && check == x86_fb_authority);
    ++video_calls;
    return check(authority);
}

int x86_paging_inventory_verify_lapic(const KERNEL_BOOT_CONTEXT *ctx,
    const KERNEL_PHYSICAL_ALLOCATOR *physical, const X86_LAPIC_MAPPING *mapping,
    const X86_PAGING_INVENTORY *snapshot)
{
    CHECK(ctx == &context && physical == &pool && mapping == &lapic && snapshot == &inventory);
    ++lapic_calls;
    return (int)valid;
}

// ====== verify ======

static int verify(void)
{
    return x86_fb_live_inventory(&owner, &context, &pool, &lapic, &inventory);
}

// ====== main ======

int main(void)
{
    X86_FB_MAPPING copy;
    BOOT_U32 index, saved_calls;
    CHECK(verify() && lapic_calls == 1 && !video_calls);
    valid = 0; CHECK(!verify()); valid = 1;
    for (index = 0; index < sizeof(owner); ++index) {
        ((unsigned char *)&owner)[index] = 1;
        CHECK(!verify());
        ((unsigned char *)&owner)[index] = 0;
    }
    CHECK(lapic_calls == 2);
    context.Info.Framebuffer.Base = 0x10000000;
    CHECK(!verify() && !video_calls && lapic_calls == 2);
    owner.Self = (BOOT_U64)&owner; owner.Ready = 2; owner.Published = 1;
    owner.Context = &context; owner.Lapic = &lapic; owner.Inventory = &inventory;
    owner.Window = &window; window.Pool = &pool;
    for (index = 1; index <= X86_PAGING_VIDEO_TABLES; ++index) {
        owner.Allocated = owner.Authority.Region.TableCount = index;
        owner.Pinned = (1U << index) - 1U;
        CHECK(verify());
        owner.Pinned ^= 1; CHECK(!verify()); owner.Pinned ^= 1;
    }
    saved_calls = video_calls;
    copy = owner;
    CHECK(!x86_fb_live_inventory(&copy, &context, &pool, &lapic, &inventory));
    owner.Ready = 1; CHECK(!verify()); owner.Ready = 3; CHECK(!verify()); owner.Ready = 2;
    owner.Published = 0; CHECK(!verify()); owner.Published = 1;
    owner.Context = &other_context; CHECK(!verify()); owner.Context = &context;
    owner.Inventory = NULL; CHECK(!verify()); owner.Inventory = &inventory;
    owner.Window = NULL; CHECK(!verify()); owner.Window = &window;
    window.Pool = NULL; CHECK(!verify()); window.Pool = &pool;
    owner.Allocated = 32; CHECK(!verify()); owner.Allocated = X86_PAGING_VIDEO_TABLES;
    owner.Authority.Region.TableCount = 1; CHECK(!verify());
    owner.Authority.Region.TableCount = X86_PAGING_VIDEO_TABLES;
    CHECK(video_calls == saved_calls);
    valid = 0; CHECK(!verify() && lapic_calls == 2); valid = 1;
    CHECK(!x86_fb_live_inventory(NULL, &context, &pool, &lapic, &inventory));
    CHECK(!x86_fb_live_inventory(&owner, NULL, &pool, &lapic, &inventory));
    CHECK(!x86_fb_live_inventory(&owner, &context, NULL, &lapic, &inventory));
    CHECK(!x86_fb_live_inventory(&owner, &context, &pool, NULL, &inventory));
    CHECK(!x86_fb_live_inventory(&owner, &context, &pool, &lapic, NULL));
    CHECK(verify());
    printf("PASS live inventory gateway: %u checks; explicit owner, fail closed, no allocations\n", checks);
    return 0;
}
