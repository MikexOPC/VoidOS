#include "arch/x86/kernel/timer/hpet/watchdog/watchdog.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static X86_HPET_WATCHDOG owner;
static X86_PAGE_WINDOW window;
static KERNEL_PHYSICAL_ALLOCATOR pool;
static KERNEL_PHYSICAL_LEASE canonical[6];
static uint32_t checks, context = 1, pins[6] = {1,1,1,1,1,1};

// ====== check_private ======

static void check_private(int condition)
{
    ++checks;
    if (!condition) { fprintf(stderr,"Budget check %u failed\n",checks); exit(1); }
}

// ====== allocator_and_environment_mocks ======

X86_HPET_WATCHDOG *x86_watchdog_state(void) { return &owner; }
int x86_watchdog_valid(void)
{
    return context == 1 && owner.Self == (BOOT_U64)&owner && owner.Ready && !owner.Closed &&
        x86_watchdog_budget_valid();
}
int kernel_physical_validate(const KERNEL_PHYSICAL_ALLOCATOR *input) { return input == &pool; }
int kernel_physical_is_pinned(const KERNEL_PHYSICAL_ALLOCATOR *input, const KERNEL_PHYSICAL_LEASE *lease)
{
    if (input != &pool || !lease) return -1;
    for (uint32_t index = 0; index < 6; ++index)
        if (!memcmp(lease,&canonical[index],sizeof(*lease))) return (int)pins[index];
    return -1;
}

// ====== scope_lifecycle ======

static void scope_lifecycle(void)
{
    KERNEL_PHYSICAL_LEASE first = canonical[0], second = canonical[1];
    check_private(x86_watchdog_valid());
    pool.FreePages = 68;
    check_private(!x86_watchdog_valid());
    check_private(!x86_watchdog_budget_begin(NULL,&second));
    check_private(!x86_watchdog_budget_begin(&first,&first));
    first.Pages = 8;
    check_private(!x86_watchdog_budget_begin(&first,&second));
    first = canonical[0]; context = 0;
    check_private(!x86_watchdog_budget_begin(&first,&second));
    check_private(!x86_watchdog_budget_active() && pool.FreePages == 68 && owner.Budget == 100);
    context = 1;
    check_private(x86_watchdog_budget_begin(&first,&second));
    check_private(x86_watchdog_budget_active() && x86_watchdog_valid());
    check_private(!x86_watchdog_budget_begin(&first,&second));
    first.Id = 999; // Accounting copied identity, does not borrow mutable caller storage.
    check_private(x86_watchdog_valid());
    first = canonical[0]; pins[1] = 0;
    check_private(!x86_watchdog_valid() && !x86_watchdog_budget_end() && x86_watchdog_budget_active());
    pins[1] = 1; pool.FreePages = 69;
    check_private(!x86_watchdog_valid() && !x86_watchdog_budget_end());
    pool.FreePages = 68; owner.Armed = 1;
    check_private(!x86_watchdog_budget_end());
    owner.Armed = 0; context = 0;
    check_private(!x86_watchdog_budget_end());
    context = 1;
    check_private(x86_watchdog_budget_end());
    check_private(!x86_watchdog_budget_active() && !x86_watchdog_valid());
    check_private(!x86_watchdog_budget_end());
    pool.FreePages = 100;
    check_private(x86_watchdog_valid() && owner.Budget == 100);
}

// ====== main ======

static void three_lifecycle(void)
{
    KERNEL_PHYSICAL_LEASE third = canonical[2];
    pool.FreePages = 52;
    check_private(!x86_watchdog_budget_begin(&canonical[0],&canonical[1]));
    check_private(!x86_watchdog_budget_begin_three(&canonical[0],&canonical[1],&canonical[0]));
    third.Pages = 8;
    check_private(!x86_watchdog_budget_begin_three(&canonical[0],&canonical[1],&third));
    third = canonical[2]; pins[2] = 0;
    check_private(!x86_watchdog_budget_begin_three(&canonical[0],&canonical[1],&third));
    pins[2] = 1;
    check_private(x86_watchdog_budget_begin_three(&canonical[0],&canonical[1],&third));
    third.Id = 999; check_private(x86_watchdog_valid());
    pins[2] = 0;
    check_private(!x86_watchdog_valid() && !x86_watchdog_budget_end() && x86_watchdog_budget_active());
    pins[2] = 1; ++pool.FreePages;
    check_private(!x86_watchdog_valid() && !x86_watchdog_budget_end());
    pool.FreePages = 52;
    check_private(x86_watchdog_budget_end() && !x86_watchdog_valid());
    pool.FreePages = 100;
    check_private(x86_watchdog_valid());
}

// ====== main ======

// ====== four_lifecycle ======

static void four_lifecycle(void)
{
    KERNEL_PHYSICAL_LEASE fourth = canonical[3];
    pool.FreePages = 36;
    check_private(!x86_watchdog_budget_begin_three(&canonical[0],&canonical[1],&canonical[2]));
    check_private(!x86_watchdog_budget_begin_four(&canonical[0],&canonical[1],NULL,&fourth));
    check_private(!x86_watchdog_budget_begin_four(&canonical[0],&canonical[1],&canonical[2],&canonical[0]));
    fourth.Pages = 8;
    check_private(!x86_watchdog_budget_begin_four(&canonical[0],&canonical[1],&canonical[2],&fourth));
    fourth = canonical[3]; pins[3] = 0;
    check_private(!x86_watchdog_budget_begin_four(&canonical[0],&canonical[1],&canonical[2],&fourth));
    pins[3] = 1; context = 0;
    check_private(!x86_watchdog_budget_begin_four(&canonical[0],&canonical[1],&canonical[2],&fourth));
    check_private(!x86_watchdog_budget_active()); context = 1;
    check_private(x86_watchdog_budget_begin_four(&canonical[0],&canonical[1],&canonical[2],&fourth));
    fourth.Id = 999; check_private(x86_watchdog_valid());
    pins[3] = 0; check_private(!x86_watchdog_valid() && !x86_watchdog_budget_end());
    pins[3] = 1; ++pool.FreePages; check_private(!x86_watchdog_valid() && !x86_watchdog_budget_end());
    pool.FreePages = 36; check_private(x86_watchdog_budget_end());
    pool.FreePages = 100; check_private(x86_watchdog_valid());
}

// ====== lend_lifecycle ======

static void lend_lifecycle(void)
{
    KERNEL_PHYSICAL_LEASE extra = canonical[4];
    check_private(!x86_watchdog_budget_lend(&extra));
    pool.FreePages = 36;
    check_private(x86_watchdog_budget_begin_four(&canonical[0],&canonical[1],&canonical[2],&canonical[3]));
    check_private(!x86_watchdog_budget_lend(&extra));
    pool.FreePages = 20;
    check_private(!x86_watchdog_valid());
    check_private(!x86_watchdog_budget_lend(&canonical[0]));
    extra.Pages = 8; check_private(!x86_watchdog_budget_lend(&extra)); extra = canonical[4];
    pins[4] = 0; check_private(!x86_watchdog_budget_lend(&extra)); pins[4] = 1;
    extra.Base = canonical[3].Base + 4096; check_private(!x86_watchdog_budget_lend(&extra)); extra = canonical[4];
    owner.Armed = 1; check_private(!x86_watchdog_budget_lend(&extra)); owner.Armed = 0;
    check_private(x86_watchdog_budget_lend(&extra) && x86_watchdog_valid());
    check_private(!x86_watchdog_budget_lend(&extra));
    pool.FreePages = 4;
    check_private(x86_watchdog_budget_lend(&canonical[5]) && x86_watchdog_valid());
    check_private(!x86_watchdog_budget_return(&canonical[0]));
    extra.Base += 65536; check_private(!x86_watchdog_budget_return(&extra)); extra = canonical[4];
    check_private(x86_watchdog_budget_return(&extra) && !x86_watchdog_valid());
    check_private(!x86_watchdog_budget_return(&extra));
    pool.FreePages = 20; check_private(x86_watchdog_valid());
    check_private(x86_watchdog_budget_return(&canonical[5]));
    pool.FreePages = 36; check_private(x86_watchdog_valid());
    check_private(x86_watchdog_budget_end());
    pool.FreePages = 100; check_private(x86_watchdog_valid() && !x86_watchdog_budget_lend(&extra));
}

int main(void)
{
    window.Pool = &pool; owner.Env.Window = &window;
    owner.Self = (BOOT_U64)&owner; owner.Ready = 1; owner.Budget = pool.FreePages = 100;
    canonical[0] = (KERNEL_PHYSICAL_LEASE){0x200000,16,11,(BOOT_U64)&pool};
    canonical[1] = (KERNEL_PHYSICAL_LEASE){0x300000,16,12,(BOOT_U64)&pool};
    canonical[2] = (KERNEL_PHYSICAL_LEASE){0x400000,16,13,(BOOT_U64)&pool};
    canonical[3] = (KERNEL_PHYSICAL_LEASE){0x500000,16,14,(BOOT_U64)&pool};
    canonical[4] = (KERNEL_PHYSICAL_LEASE){0x600000,16,15,(BOOT_U64)&pool};
    canonical[5] = (KERNEL_PHYSICAL_LEASE){0x700000,16,16,(BOOT_U64)&pool};
    for (uint32_t iteration = 0; iteration < 10000; ++iteration) { scope_lifecycle(); three_lifecycle(); four_lifecycle(); lend_lifecycle(); }
    pool.FreePages = 68;
    canonical[1].Base = canonical[0].Base + 4096;
    check_private(!x86_watchdog_budget_begin(&canonical[0],&canonical[1]));
    check_private(!x86_watchdog_budget_active());
    pool.FreePages = 100;
    printf("PASS: %u watchdog loan checks, 10000 scopes, allocator/CPU MOCK\n",checks);
    return 0;
}
