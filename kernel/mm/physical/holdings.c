#include "holdings.h"
static KERNEL_HOLDING *holders[KERNEL_HOLDINGS];

// ====== find_private ======

static BOOT_U32 find_private(const KERNEL_HOLDING *holder)
{
    for (BOOT_U32 index = 0; index < KERNEL_HOLDINGS; ++index) if (holders[index] == holder) return index;

    return KERNEL_HOLDINGS;
}

// ====== kernel_holdings_register ======

int kernel_holdings_register(KERNEL_HOLDING *holder, BOOT_U64 pages)
{
    if (!holder || find_private(holder) != KERNEL_HOLDINGS) return 0;

    BOOT_U32 slot = find_private(0);

    if (slot == KERNEL_HOLDINGS) return 0;

    holder->Self = (BOOT_U64)holder;
    holder->Pages = pages;
    holders[slot] = holder;
    return 1;
}

// ====== kernel_holdings_update ======

int kernel_holdings_update(KERNEL_HOLDING *holder, BOOT_U64 pages)
{
    if (!holder || holder->Self != (BOOT_U64)holder || find_private(holder) == KERNEL_HOLDINGS) return 0;

    holder->Pages = pages;
    return 1;
}

// ====== kernel_holdings_unregister ======

int kernel_holdings_unregister(KERNEL_HOLDING *holder)
{
    BOOT_U32 slot = holder ? find_private(holder) : KERNEL_HOLDINGS;

    if (slot == KERNEL_HOLDINGS) return 0;

    holders[slot] = 0;
    *holder = (KERNEL_HOLDING){0};
    return 1;
}

// ====== kernel_holdings_total ======

BOOT_U64 kernel_holdings_total(void)
{
    BOOT_U64 total = 0;

    for (BOOT_U32 index = 0; index < KERNEL_HOLDINGS; ++index) {
        const KERNEL_HOLDING *holder = holders[index];

        if (!holder) continue;

        if (holder->Self != (BOOT_U64)holder || holder->Pages > UINT64_MAX - total) return UINT64_MAX;

        total += holder->Pages;
    }

    return total;
}
