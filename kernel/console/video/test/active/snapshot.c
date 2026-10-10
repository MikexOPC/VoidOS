#include "snapshot.h"
#include "arch/x86/kernel/exceptions/terminal.h"

// ====== kernel_console_active_cell ======

int kernel_console_active_cell(X86_FB_MAPPING *owner, BOOT_U64 pages,
    BOOT_U32 column, BOOT_U32 row, BOOT_U32 scalar, BOOT_U32 foreground,
    BOOT_U32 background, BOOT_U64 *ticks)
{
    X86_PAGING_INVENTORY expected, *original;
    int result;
    if (!owner || owner->Self != (BOOT_U64)owner || owner->Ready != 2 || owner->Published != 1 ||
        pages != 48 || owner->Allocated < 3 || owner->Allocated > X86_PAGING_VIDEO_TABLES ||
        !x86_exception_environment() || !(original = owner->Inventory) ||
        !owner->Window || !owner->Window->Pool || owner->Budget < owner->Allocated + pages ||
        owner->Window->Pool->FreePages != owner->Budget - owner->Allocated - pages ||
        original->LeafPages > UINT64_MAX - pages || original->RamPages > UINT64_MAX - pages) return 0;
    expected = *original;
    expected.LeafPages += pages;
    expected.RamPages += pages;
    owner->Inventory = &expected;
    result = x86_fb_cell(owner, column, row, scalar, foreground, background, ticks);
    owner->Inventory = original;
    return result;
}
