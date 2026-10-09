#ifndef OS_X86_FRAMEBUFFER_KERNEL_H
#define OS_X86_FRAMEBUFFER_KERNEL_H
#include "../mapping.h"
int x86_fb_kernel_start(const KERNEL_BOOT_CONTEXT *context, X86_PAGE_WINDOW *window,
    const X86_LAPIC_MAPPING *lapic, X86_PAGING_INVENTORY *inventory);
#endif
