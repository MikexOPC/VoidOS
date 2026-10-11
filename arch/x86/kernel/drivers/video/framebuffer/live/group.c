#include "group.h"
#include "arch/x86/kernel/scheduler/mapping/native.h"

// ====== prepare ======

static int prepare(X86_FB_MAPPING *owner, X86_PAGING_INVENTORY *expected, BOOT_U64 *pages)
{
    const X86_PAGING_INVENTORY *original;
    if (!owner || owner->Self != (BOOT_U64)owner || owner->Ready != 2 || owner->Published != 1 ||
        !owner->Context || !owner->Window || !owner->Window->Pool || !(original = owner->Inventory) ||
        owner->Allocated < 3 || owner->Allocated > X86_PAGING_VIDEO_TABLES) return 0;
    *pages = x86_scheduler_live_stack_pages_for(owner->Window->Pool, owner->Authority.Root);
    if (!*pages || *pages > 128 || *pages % 16 || owner->Budget < owner->Allocated + *pages ||
        owner->Window->Pool->FreePages != owner->Budget - owner->Allocated - *pages ||
        original->LeafPages > UINT64_MAX - *pages || original->RamPages > UINT64_MAX - *pages) return 0;
    *expected = *original;
    expected->LeafPages += *pages;
    expected->RamPages += *pages;
    return 1;
}

// ====== x86_fb_live_group ======

int x86_fb_live_group(X86_FB_MAPPING *owner, BOOT_U32 column, BOOT_U32 row,
    const BOOT_U32 *scalars, BOOT_U32 count, BOOT_U32 foreground, BOOT_U32 background, BOOT_U64 *ticks)
{
    X86_PAGING_INVENTORY expected, *original;
    BOOT_U64 pages;
    int result;
    if (!ticks || !scalars || !count || count > 8 || !prepare(owner, &expected, &pages)) return 0;
    original = owner->Inventory;
    owner->Inventory = &expected;
    result = x86_fb_group(owner, column, row, scalars, count, foreground, background, ticks);
    owner->Inventory = original;
    return result && pages == x86_scheduler_live_stack_pages_for(owner->Window->Pool, owner->Authority.Root) &&
        owner->Window->Pool->FreePages == owner->Budget - owner->Allocated - pages;
}
