#ifndef OS_X86_FRAMEBUFFER_KERNEL_H
#define OS_X86_FRAMEBUFFER_KERNEL_H
#include "../mapping.h"
int x86_fb_kernel_start(const KERNEL_BOOT_CONTEXT *context, X86_PAGE_WINDOW *window,
    const X86_LAPIC_MAPPING *lapic, X86_PAGING_INVENTORY *inventory);
int x86_fb_kernel_cell(BOOT_U32 column, BOOT_U32 row, BOOT_U32 scalar,
    BOOT_U32 foreground, BOOT_U32 background, BOOT_U64 *ticks);
#endif
