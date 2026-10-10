#ifndef X86_FB_RESOURCES_INTEGRATION_H
#define X86_FB_RESOURCES_INTEGRATION_H
#include "kernel.h"

// ====== x86_device_inventory_verify ======

static inline int x86_device_inventory_verify(const KERNEL_BOOT_CONTEXT *context,
    const KERNEL_PHYSICAL_ALLOCATOR *pool, const X86_LAPIC_MAPPING *lapic,
    const X86_PAGING_INVENTORY *inventory)
{
#ifdef KERNEL_TEST_CONSOLE_ACTIVE
    return x86_fb_kernel_inventory_verify(context, pool, lapic, inventory);
#else
    return x86_paging_inventory_verify_lapic(context, pool, lapic, inventory);
#endif
}
#endif
