#include "snapshot.h"
#include "arch/x86/kernel/exceptions/terminal.h"

// ====== prepare ======

static int prepare(X86_FB_MAPPING *owner, BOOT_U64 pages, X86_PAGING_INVENTORY *expected)
{
    X86_PAGING_INVENTORY *original;
    if (!owner || owner->Self != (BOOT_U64)owner || owner->Ready != 2 || owner->Published != 1 ||
        pages != 48 || owner->Allocated < 3 || owner->Allocated > X86_PAGING_VIDEO_TABLES ||
        !x86_exception_environment() || !(original = owner->Inventory) ||
        !owner->Window || !owner->Window->Pool || owner->Budget < owner->Allocated + pages ||
        owner->Window->Pool->FreePages != owner->Budget - owner->Allocated - pages ||
        original->LeafPages > UINT64_MAX - pages || original->RamPages > UINT64_MAX - pages) return 0;
    *expected = *original;
    expected->LeafPages += pages;
    expected->RamPages += pages;
    return 1;
}

// ====== kernel_console_active_cell ======

int kernel_console_active_cell(X86_FB_MAPPING *owner, BOOT_U64 pages,
    BOOT_U32 column, BOOT_U32 row, BOOT_U32 scalar, BOOT_U32 foreground,
    BOOT_U32 background, BOOT_U64 *ticks)
{
    X86_PAGING_INVENTORY expected, *original;
    int result;
    if (!prepare(owner, pages, &expected)) return 0;
    original = owner->Inventory;
    owner->Inventory = &expected;
    result = x86_fb_cell(owner, column, row, scalar, foreground, background, ticks);
    owner->Inventory = original;
    return result;
}

// ====== kernel_console_active_group ======

int kernel_console_active_group(X86_FB_MAPPING *owner, BOOT_U64 pages,
    BOOT_U32 column, BOOT_U32 row, const BOOT_U32 *scalars, BOOT_U32 count,
    BOOT_U32 foreground, BOOT_U32 background, BOOT_U64 *ticks)
{
    X86_PAGING_INVENTORY expected, *original;
    int result;
    if (!scalars || !count || count > 8 || !prepare(owner, pages, &expected)) return 0;
    original = owner->Inventory;
    owner->Inventory = &expected;
    result = x86_fb_group(owner, column, row, scalars, count, foreground, background, ticks);
    owner->Inventory = original;
    return result;
}
