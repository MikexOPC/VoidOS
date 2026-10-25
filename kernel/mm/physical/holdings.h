#ifndef OS_KERNEL_PHYSICAL_HOLDINGS_H
#define OS_KERNEL_PHYSICAL_HOLDINGS_H
#include "kernel/mm/inventory.h"

// G5 V3: pages held for a whole session by registered owners (the persistent GPU session today, a
// growing heap later). Budget checks subtract the declared total instead of expecting FreePages
// to stay constant. A holder updates Pages when it takes or returns pages.
#define KERNEL_HOLDINGS 8U
typedef struct {
    BOOT_U64  Self;
    BOOT_U64  Pages;
} KERNEL_HOLDING;

int       kernel_holdings_register(KERNEL_HOLDING *holder, BOOT_U64 pages);
int       kernel_holdings_update(KERNEL_HOLDING *holder, BOOT_U64 pages);
int       kernel_holdings_unregister(KERNEL_HOLDING *holder);
// Sum of the declared pages; UINT64_MAX when a registered holder is no longer valid.
BOOT_U64  kernel_holdings_total(void);
#endif
