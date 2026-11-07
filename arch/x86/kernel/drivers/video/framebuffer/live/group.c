#include "group.h"
#include "arch/x86/kernel/scheduler/mapping/native.h"
#include "../console/profile.h"
#include "../pixels/session.h"
#include "kernel/mm/physical/holdings.h"
// ====== budget_matches ======

static int budget_matches(const X86_FB_MAPPING *owner, const X86_PAGING_INVENTORY *inventory, BOOT_U64 pages)
{
    BOOT_U64 mapped = (BOOT_U64)owner->Allocated + pages + (inventory->Gpu.Ready == 1 ? X86_GPU_TABLES : 0);
    BOOT_U64 held = kernel_holdings_total();
    return held != UINT64_MAX && mapped <= owner->Budget && held <= owner->Budget - mapped &&
        kernel_physical_budget_matches(owner->Window->Pool, owner->Budget - mapped - held);
}

// ====== prepare ======

static int prepare(X86_FB_MAPPING *owner, X86_PAGING_INVENTORY *expected, BOOT_U64 *pages)
{
    const X86_PAGING_INVENTORY *original;
    if (!owner || owner->Self != (BOOT_U64)owner || owner->Ready != 2 || owner->Published != 1 ||
        !owner->Context || !owner->Window || !owner->Window->Pool || !(original = owner->Inventory) ||
        owner->Allocated < 3 || owner->Allocated > X86_PAGING_VIDEO_TABLES) return 0;
    *pages = x86_scheduler_live_stack_pages_for(owner->Window->Pool, owner->Authority.Root);
    if (original->Gpu.Ready > 1 || !*pages || *pages > 128 || *pages % 16 ||
        !budget_matches(owner, original, *pages) ||
        original->LeafPages > UINT64_MAX - *pages || original->RamPages > UINT64_MAX - *pages) return 0;
    *expected = *original;
    expected->LeafPages += *pages;
    expected->RamPages += *pages;
    return 1;
}

// ====== x86_fb_live_group_pointer ======

int x86_fb_live_group_pointer(X86_FB_MAPPING *owner, BOOT_U32 column, BOOT_U32 row,
    const BOOT_U32 *scalars, BOOT_U32 count, BOOT_U32 foreground, BOOT_U32 background,
    BOOT_U64 *ticks, BOOT_U32 caret, const KERNEL_POINTER_IMAGE *pointer)
{
    X86_PAGING_INVENTORY expected, *original;
    BOOT_U64 pages;
    int result;
    if (!ticks || !scalars || !count || count > 8) return 0;

    X86_PROFILE_BEGIN(prepare_start);
    int prepared = prepare(owner, &expected, &pages);
    X86_PROFILE_END(X86_PROFILE_PREPARE, prepare_start);

    if (!prepared) return 0;
    original = owner->Inventory;
    owner->Inventory = &expected;
    result = pointer ? x86_fb_group_pointer(owner, column, row, scalars, count, foreground, background, ticks, caret, pointer) :
        x86_fb_group_caret(owner, column, row, scalars, count, foreground, background, ticks, caret);
    owner->Inventory = original;
    return result && pages == x86_scheduler_live_stack_pages_for(owner->Window->Pool, owner->Authority.Root) &&
        budget_matches(owner, original, pages);
}

// ====== x86_fb_live_audit ======

// G5 V1-A: the idle audit. Same expected inventory as a drawing group (the live task stacks
// added), one full scan, and the same accounting check afterwards.
int x86_fb_live_audit(X86_FB_MAPPING *owner)
{
    X86_PAGING_INVENTORY expected, *original;
    BOOT_U64 pages;

    if (!prepare(owner, &expected, &pages)) return 0;

    original = owner->Inventory;
    owner->Inventory = &expected;
    int audited = x86_fb_audit(owner);
    owner->Inventory = original;
    return audited && pages == x86_scheduler_live_stack_pages_for(owner->Window->Pool, owner->Authority.Root) &&
        budget_matches(owner, original, pages);
}

// ====== x86_fb_live_group_caret ======

int x86_fb_live_group_caret(X86_FB_MAPPING *owner, BOOT_U32 column, BOOT_U32 row,
    const BOOT_U32 *scalars, BOOT_U32 count, BOOT_U32 foreground, BOOT_U32 background,
    BOOT_U64 *ticks, BOOT_U32 caret)
{
    return x86_fb_live_group_pointer(owner, column, row, scalars, count, foreground, background, ticks, caret, NULL);
}

// ====== x86_fb_live_group ======

int x86_fb_live_group(X86_FB_MAPPING *owner, BOOT_U32 column, BOOT_U32 row,
    const BOOT_U32 *scalars, BOOT_U32 count, BOOT_U32 foreground, BOOT_U32 background, BOOT_U64 *ticks)
{
    return x86_fb_live_group_caret(owner, column, row, scalars, count, foreground, background, ticks, ~0U);
}
