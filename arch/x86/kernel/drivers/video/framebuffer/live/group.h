#ifndef X86_FB_LIVE_GROUP_H
#define X86_FB_LIVE_GROUP_H
#include "../gateway/cell.h"
int x86_fb_live_group(X86_FB_MAPPING *owner, BOOT_U32 column, BOOT_U32 row,
    const BOOT_U32 *scalars, BOOT_U32 count, BOOT_U32 foreground, BOOT_U32 background, BOOT_U64 *ticks);
#endif
