#ifndef X86_FB_INVENTORY_GATEWAY_H
#define X86_FB_INVENTORY_GATEWAY_H
#include "../mapping.h"
int x86_fb_live_inventory(const X86_FB_MAPPING *owner, const KERNEL_BOOT_CONTEXT *context,
    const KERNEL_PHYSICAL_ALLOCATOR *pool, const X86_LAPIC_MAPPING *lapic,
    const X86_PAGING_INVENTORY *inventory);
#endif
