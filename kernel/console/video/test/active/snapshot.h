#ifndef KERNEL_CONSOLE_ACTIVE_SNAPSHOT_H
#define KERNEL_CONSOLE_ACTIVE_SNAPSHOT_H
#include "arch/x86/kernel/drivers/video/framebuffer/gateway/cell.h"
int kernel_console_active_cell(X86_FB_MAPPING *owner, BOOT_U64 pages,
    BOOT_U32 column, BOOT_U32 row, BOOT_U32 scalar, BOOT_U32 foreground,
    BOOT_U32 background, BOOT_U64 *ticks);
#endif
