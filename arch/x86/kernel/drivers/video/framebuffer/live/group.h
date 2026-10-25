#ifndef X86_FB_LIVE_GROUP_H
#define X86_FB_LIVE_GROUP_H
#include "../gateway/cell.h"
int x86_fb_live_group_pointer(X86_FB_MAPPING *owner, BOOT_U32 column, BOOT_U32 row,
    const BOOT_U32 *scalars, BOOT_U32 count, BOOT_U32 foreground, BOOT_U32 background,
    BOOT_U64 *ticks, BOOT_U32 caret, const KERNEL_POINTER_IMAGE *pointer);
int x86_fb_live_group_caret(X86_FB_MAPPING *owner, BOOT_U32 column, BOOT_U32 row,
    const BOOT_U32 *scalars, BOOT_U32 count, BOOT_U32 foreground, BOOT_U32 background, BOOT_U64 *ticks, BOOT_U32 caret);
// G5: full paging audit from the console task (idle), with the live expected inventory.
int x86_fb_live_audit(X86_FB_MAPPING *owner);
int x86_fb_live_group(X86_FB_MAPPING *owner, BOOT_U32 column, BOOT_U32 row,
    const BOOT_U32 *scalars, BOOT_U32 count, BOOT_U32 foreground, BOOT_U32 background, BOOT_U64 *ticks);
#endif
