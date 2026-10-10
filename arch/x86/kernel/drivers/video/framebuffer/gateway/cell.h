#ifndef X86_FB_CELL_GATEWAY_H
#define X86_FB_CELL_GATEWAY_H
#include "../mapping.h"
int x86_fb_cell(X86_FB_MAPPING *owner, BOOT_U32 column, BOOT_U32 row, BOOT_U32 scalar,
    BOOT_U32 foreground, BOOT_U32 background, BOOT_U64 *ticks);
int x86_fb_group(X86_FB_MAPPING *owner, BOOT_U32 column, BOOT_U32 row,
    const BOOT_U32 *scalars, BOOT_U32 count, BOOT_U32 foreground, BOOT_U32 background, BOOT_U64 *ticks);
int x86_fb_cell_clock_available(void);
BOOT_U64 x86_fb_cell_stamp(void);
BOOT_U64 x86_fb_cell_suspend(void);
int x86_fb_cell_resume(BOOT_U64 flags);
#endif
