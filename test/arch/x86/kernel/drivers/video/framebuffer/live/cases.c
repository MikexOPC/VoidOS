#include "arch/x86/kernel/drivers/video/framebuffer/live/group.h"
#include "kernel/mm/physical/holdings.h"
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

// ====== x86_fb_audit ======

// G5: the audit scans with the live expected inventory (task stacks added) and restores the owner.
static BOOT_U32 audits, audit_fail;

int x86_fb_audit(X86_FB_MAPPING *mapping)
{
    CHECK(mapping == &owner && mapping->Inventory != &inventory);
    CHECK(mapping->Inventory->LeafPages == inventory.LeafPages + pages);
    ++audits;
    return !audit_fail;
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
        setup(count); inventory.Gpu.Ready = 1; pool.FreePages -= X86_GPU_TABLES;
        CHECK(run() && calls == 1);
        --pool.FreePages; CHECK(!run() && calls == 1);
        setup(count); inventory.Gpu.Ready = 1; CHECK(!run() && !calls);
        setup(count); inventory.Gpu.Ready = 2; CHECK(!run() && !calls);
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
    // G5 V1-A: the idle audit uses the same expected inventory and accounting as a group.
    setup(48); audits = audit_fail = 0;
    CHECK(x86_fb_live_audit(&owner) && audits == 1 && owner.Inventory == &inventory);
    audit_fail = 1; CHECK(!x86_fb_live_audit(&owner) && audits == 2 && owner.Inventory == &inventory);
    audit_fail = 0; --pool.FreePages; CHECK(!x86_fb_live_audit(&owner));
    // G5 V3: pages declared by a registered holder are subtracted from the budget.
    static KERNEL_HOLDING session;
    setup(48); pool.FreePages -= 7;
    CHECK(!x86_fb_live_audit(&owner) && kernel_holdings_register(&session, 7) && x86_fb_live_audit(&owner));
    CHECK(kernel_holdings_total() == 7 && kernel_holdings_update(&session, 9) && !x86_fb_live_audit(&owner));
    CHECK(kernel_holdings_update(&session, 7) && x86_fb_live_audit(&owner));
    CHECK(!kernel_holdings_register(&session, 1) && kernel_holdings_unregister(&session) && !kernel_holdings_unregister(&session));
    CHECK(kernel_holdings_total() == 0 && !x86_fb_live_audit(&owner));
    printf("PASS live group: %u checks; authority/CPU/paging MOCK; scoped snapshot restored\n", checks);
    return 0;
}
