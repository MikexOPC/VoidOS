#include "arch/x86/kernel/drivers/video/framebuffer/live/group.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static KERNEL_BOOT_CONTEXT context;
static KERNEL_PHYSICAL_ALLOCATOR pool, foreign;
static X86_PAGE_WINDOW window;
static X86_PAGING_INVENTORY inventory;
static X86_FB_MAPPING owner;
static BOOT_U64 pages;
static BOOT_U32 checks, calls, fail, changed;
#define CHECK(value) do { ++checks; if (!(value)) { \
    fprintf(stderr, "FAIL live group line %d\n", __LINE__); exit(1); } } while (0)

// ====== x86_scheduler_live_stack_pages_for ======

uint64_t x86_scheduler_live_stack_pages_for(const KERNEL_PHYSICAL_ALLOCATOR *physical, uint64_t root)
{
    return physical == &pool && root == 0x200000 ? pages : UINT64_MAX;
}

// ====== x86_fb_group ======

int x86_fb_group_caret(X86_FB_MAPPING *mapping, BOOT_U32 column, BOOT_U32 row,
    const BOOT_U32 *scalars, BOOT_U32 count, BOOT_U32 foreground, BOOT_U32 background, BOOT_U64 *ticks, BOOT_U32 caret)
{
    CHECK(caret == ~0U || caret == 0);
    if (!ticks || !scalars) return 0;
    CHECK(mapping == &owner && column == 2 && row == 3 && scalars[0] == 'A' && count == 1);
    CHECK(foreground == 1 && background == 2 && mapping->Inventory != &inventory);
    CHECK(mapping->Inventory->LeafPages == inventory.LeafPages + pages);
    CHECK(mapping->Inventory->RamPages == inventory.RamPages + pages);
    ++calls; *ticks = 1;
    if (changed) --pool.FreePages;
    return !fail;
}

// ====== x86_fb_group_pointer ======

int x86_fb_group_pointer(X86_FB_MAPPING *mapping, BOOT_U32 column, BOOT_U32 row,
    const BOOT_U32 *scalars, BOOT_U32 count, BOOT_U32 foreground, BOOT_U32 background,
    BOOT_U64 *ticks, BOOT_U32 caret, const KERNEL_POINTER_IMAGE *pointer)
{
    CHECK(pointer != NULL);
    return x86_fb_group_caret(mapping, column, row, scalars, count, foreground, background, ticks, caret);
}

// ====== setup ======

static void setup(BOOT_U64 count)
{
    memset(&owner, 0, sizeof(owner)); memset(&inventory, 0, sizeof(inventory));
    window.Pool = &pool; owner.Window = &window; owner.Context = &context;
    owner.Self = (BOOT_U64)&owner; owner.Ready = 2; owner.Published = 1;
    owner.Inventory = &inventory; owner.Authority.Root = 0x200000;
    owner.Allocated = 3; owner.Budget = 1000; pool.FreePages = 997 - count;
    pages = count; fail = changed = calls = 0;
    inventory.LeafPages = 64; inventory.RamPages = 60;
}

// ====== run ======

static int run(void)
{
    const BOOT_U32 scalar = 'A'; BOOT_U64 ticks = 0;
    int result = x86_fb_live_group(&owner, 2, 3, &scalar, 1, 1, 2, &ticks);
    CHECK(owner.Inventory == &inventory);
    return result;
}

// ====== main ======

int main(void)
{
    for (BOOT_U64 count = 16; count <= 128; count += 16) {
        setup(count); CHECK(run() && calls == 1);
        fail = 1; CHECK(!run() && calls == 2);
        fail = 0; changed = 1; CHECK(!run() && calls == 3);
    }
    setup(48); window.Pool = &foreign; CHECK(!run() && !calls);
    setup(48); owner.Authority.Root = 0x201000; CHECK(!run() && !calls);
    for (BOOT_U64 count = 0; count <= 145; ++count) {
        setup(count);
        if (!count || count > 128 || count % 16) CHECK(!run() && !calls);
    }
    setup(48); pages = UINT64_MAX; CHECK(!run() && !calls);
    setup(48); --pool.FreePages; CHECK(!run() && !calls);
    setup(48); inventory.LeafPages = UINT64_MAX - 47; CHECK(!run() && !calls);
    setup(48); inventory.RamPages = UINT64_MAX - 47; CHECK(!run() && !calls);
    setup(48); owner.Budget = 50; CHECK(!run() && !calls);
    setup(48); owner.Self = 1; CHECK(!run() && !calls);
    setup(48);
    {
        const BOOT_U32 scalar = 'A'; BOOT_U64 ticks = 0;
        CHECK(x86_fb_live_group_caret(&owner, 2, 3, &scalar, 1, 1, 2, &ticks, 0));
        CHECK(owner.Inventory == &inventory && calls == 1 && ticks == 1);
    }
    printf("PASS live group: %u checks; authority/CPU/paging MOCK; scoped snapshot restored\n", checks);
    return 0;
}
