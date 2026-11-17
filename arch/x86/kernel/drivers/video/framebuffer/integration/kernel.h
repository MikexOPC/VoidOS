#ifndef OS_X86_FRAMEBUFFER_KERNEL_H
#define OS_X86_FRAMEBUFFER_KERNEL_H
#include "../mapping.h"
#include "arch/x86/kernel/timer/lapic/timer.h"
#include "kernel/console/video/pointer/shape.h"
int x86_fb_kernel_group_pointer(BOOT_U32 column, BOOT_U32 row, const BOOT_U32 *scalars,
    BOOT_U32 count, BOOT_U32 foreground, BOOT_U32 background, BOOT_U64 *ticks,
    BOOT_U32 caret, const KERNEL_POINTER_IMAGE *pointer);
// G5: idle audit of the paging inventory from the console task.
int x86_fb_kernel_audit(const X86_PM_CLOCK *clock, BOOT_U32 *max_masked);
int x86_fb_kernel_start(const KERNEL_BOOT_CONTEXT *context, X86_PAGE_WINDOW *window,
    const X86_LAPIC_MAPPING *lapic, X86_PAGING_INVENTORY *inventory);
int x86_fb_kernel_cell(BOOT_U32 column, BOOT_U32 row, BOOT_U32 scalar,
    BOOT_U32 foreground, BOOT_U32 background, BOOT_U64 *ticks);
int x86_fb_kernel_group_caret(BOOT_U32 column, BOOT_U32 row, const BOOT_U32 *scalars,
    BOOT_U32 count, BOOT_U32 foreground, BOOT_U32 background, BOOT_U64 *ticks, BOOT_U32 caret);
int x86_fb_kernel_group(BOOT_U32 column, BOOT_U32 row, const BOOT_U32 *scalars,
    BOOT_U32 count, BOOT_U32 foreground, BOOT_U32 background, BOOT_U64 *ticks);
int x86_fb_kernel_inventory_verify(const KERNEL_BOOT_CONTEXT *context,
    const KERNEL_PHYSICAL_ALLOCATOR *pool, const X86_LAPIC_MAPPING *lapic,
    const X86_PAGING_INVENTORY *inventory);
int x86_fb_kernel_inventory_capture(const KERNEL_BOOT_CONTEXT *context,
    const KERNEL_PHYSICAL_ALLOCATOR *pool, const X86_LAPIC_MAPPING *lapic,
    X86_PAGING_INVENTORY *inventory);
#endif
