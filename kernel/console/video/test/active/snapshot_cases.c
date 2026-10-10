#include "snapshot.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static X86_FB_MAPPING owner;
static X86_PAGING_INVENTORY inventory;
static X86_PAGE_WINDOW window;
static KERNEL_PHYSICAL_ALLOCATOR pool;
static unsigned int calls, environment = 1, failure;

// ====== x86_exception_environment ======

BOOT_U64 x86_exception_environment(void) { return environment; }

// ====== x86_fb_cell ======

int x86_fb_cell(X86_FB_MAPPING *view, BOOT_U32 column, BOOT_U32 row, BOOT_U32 scalar,
    BOOT_U32 foreground, BOOT_U32 background, BOOT_U64 *ticks)
{
    X86_PAGING_INVENTORY expected = inventory;
    assert(view == &owner && column == 2 && row == 10 && scalar == 0xe8);
    assert(foreground == 0xffffff && background == 0x081018 && ticks);
    assert(view->Inventory != &inventory);
    expected.LeafPages += 48; expected.RamPages += 48;
    assert(!memcmp(&expected, view->Inventory, sizeof(expected)));
    ++calls; *ticks = 1;
    return !failure;
}

// ====== run ======

static int run(BOOT_U64 pages)
{
    BOOT_U64 ticks;
    X86_PAGING_INVENTORY before = inventory;
    int result = kernel_console_active_cell(&owner, pages, 2, 10, 0xe8, 0xffffff, 0x081018, &ticks);
    assert(owner.Inventory == &inventory && !memcmp(&inventory, &before, sizeof(before)));
    return result;
}

// ====== main ======

int main(void)
{
    owner.Self = (BOOT_U64)&owner; owner.Ready = 2; owner.Published = 1;
    owner.Inventory = &inventory; owner.Window = &window; owner.Allocated = 4; owner.Budget = 100;
    window.Pool = &pool; pool.FreePages = 48;
    inventory.LeafPages = 1000; inventory.RamPages = 232; inventory.MmioPages = 768;
    assert(run(48) && calls == 1);
    failure = 1; assert(!run(48) && calls == 2); failure = 0;
    assert(!run(0) && !run(32) && !run(UINT64_MAX));
    environment = 0; assert(!run(48)); environment = 1;
    pool.FreePages = 47; assert(!run(48)); pool.FreePages = 48;
    owner.Allocated = 0; assert(!run(48)); owner.Allocated = 4;
    owner.Budget = 51; assert(!run(48)); owner.Budget = 100;
    inventory.RamPages = UINT64_MAX; assert(!run(48)); inventory.RamPages = 232;
    inventory.LeafPages = UINT64_MAX; assert(!run(48)); inventory.LeafPages = 1000;
    owner.Self = 0; assert(!run(48));
    assert(calls == 2);
    puts("PASS: scoped task snapshot, exact delta, failure restoration and unchanged baseline (MOCK)");
    return 0;
}
