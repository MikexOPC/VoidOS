#ifndef OS_X86_FRAMEBUFFER_TRANSACTION_H
#define OS_X86_FRAMEBUFFER_TRANSACTION_H
#include "../mapping.h"
int x86_fb_preflight(const KERNEL_BOOT_CONTEXT *context, X86_PAGE_WINDOW *window,
    const X86_LAPIC_MAPPING *lapic, const X86_PAGING_INVENTORY *inventory,
    X86_FB_MAPPING *owner, FB_MAPPING_GEOMETRY *geometry, X86_FB_CPU *cpu);
int x86_fb_construct(X86_FB_MAPPING *owner);
#endif
