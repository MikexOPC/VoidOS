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
static BOOT_U64 pages, paging_epoch, interrupt_flag = 0x200;
static BOOT_U32 change_epoch, pm_counter;
static const X86_PM_CLOCK clock = {.Port = 7, .Mask = 0xffffff, .Ready = 1};
static BOOT_U32 checks, calls, fail, changed;
#define CHECK(value) do { ++checks; if (!(value)) { \
    fprintf(stderr, "FAIL live group line %d\n", __LINE__); exit(1); } } while (0)

// ====== audit_boundaries ======

uint64_t kernel_preempt_section_enter(void)
{
    uint64_t saved_if = interrupt_flag;
    interrupt_flag = 0;
    return saved_if;
}

void kernel_preempt_section_leave(uint64_t saved_if)
{
    CHECK(!interrupt_flag && saved_if == 0x200);
    interrupt_flag = saved_if;
}

void x86_scheduler_yield(void)
{
    CHECK(interrupt_flag == 0x200 && owner.Inventory == &inventory);
    pm_counter += 1000;
    if (change_epoch == 1) ++paging_epoch;
    if (change_epoch == 2) ++pool.Generation;
}

// ====== x86_pm_read ======

BOOT_U32 x86_pm_read(unsigned short port)
{
    CHECK(!interrupt_flag && port == clock.Port);
    pm_counter += 13;
    return pm_counter;
}

// ====== x86_paging_generation ======

BOOT_U64 x86_paging_generation(void) { return paging_epoch; }

int x86_fb_authority(const X86_PAGING_VIDEO_AUTH *authority) { return authority == &owner.Authority; }

// ====== x86_scheduler_live_stack_pages_for ======

uint64_t x86_scheduler_live_stack_pages_for(const KERNEL_PHYSICAL_ALLOCATOR *physical, uint64_t root)
{
    CHECK(!interrupt_flag);
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

int x86_paging_inventory_verify_video_sliced(const KERNEL_BOOT_CONTEXT *boot,
    const KERNEL_PHYSICAL_ALLOCATOR *physical, const struct X86_LAPIC_MAPPING *lapic,
    const X86_PAGING_VIDEO_AUTH *authority, X86_PAGING_VIDEO_CHECK check,
    const X86_PAGING_INVENTORY *expected, X86_PAGING_CHECKPOINT checkpoint, void *cookie)
{
    (void)lapic;
    CHECK(!interrupt_flag && boot == &context && physical == &pool && check(authority));
    CHECK(owner.Inventory == &inventory && expected != &inventory);
    CHECK(expected->LeafPages == inventory.LeafPages + pages);
    ++audits;
    return checkpoint(cookie) && !audit_fail;
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

// ====== run_audit ======

static int run_audit(void)
{
    BOOT_U32 max_masked = 0;
    int result = x86_fb_live_audit(&owner, &clock, &max_masked);
    CHECK(max_masked == 13);
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
    CHECK(run_audit() && audits == 1 && owner.Inventory == &inventory);
    audit_fail = 1; CHECK(!run_audit() && audits == 2 && owner.Inventory == &inventory);
    audit_fail = 0; --pool.FreePages; CHECK(!run_audit());
    for (change_epoch = 1; change_epoch <= 2; ++change_epoch) {
        setup(48);
        CHECK(run_audit() == X86_FB_AUDIT_RETRY);
        CHECK(owner.Inventory == &inventory && interrupt_flag == 0x200);
    }
    change_epoch = 0;
    CHECK(run_audit() == X86_FB_AUDIT_OK);
    // G5 V3: pages declared by a registered holder are subtracted from the budget.
    static KERNEL_HOLDING session;
    setup(48); pool.FreePages -= 7;
    CHECK(!run_audit() && kernel_holdings_register(&session, 7) && run_audit());
    CHECK(kernel_holdings_total() == 7 && kernel_holdings_update(&session, 9) && !run_audit());
    CHECK(kernel_holdings_update(&session, 7) && run_audit());
    CHECK(!kernel_holdings_register(&session, 1) && kernel_holdings_unregister(&session) && !kernel_holdings_unregister(&session));
    CHECK(kernel_holdings_total() == 0 && !run_audit());
    setup(48); inventory.Gpu.Ready = 1; pool.FreePages -= X86_GPU_TABLES - 1;
    CHECK(kernel_holdings_register(&session, 7));
    session.Self = 0;
    CHECK(kernel_holdings_total() == UINT64_MAX && !run_audit() && !run() && !calls);
    CHECK(kernel_holdings_unregister(&session));
    printf("PASS live group: %u checks; authority/CPU/paging MOCK; scoped snapshot restored\n", checks);
    return 0;
}
