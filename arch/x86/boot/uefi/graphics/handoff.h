#ifndef OS_BOOT_GRAPHICS_HANDOFF_H
#define OS_BOOT_GRAPHICS_HANDOFF_H
#include "gop.h"
#include "../memory/regions.h"
#include "../handoff/protocol/start_info.h"
#include "kernel/drivers/video/framebuffer/handoff.h"

EFI_STATUS boot_validate_framebuffer_snapshot(const BOOT_START_INFO *info,
    const BOOT_MEMORY_MAP *map, UINT32 bits);
EFI_STATUS boot_attach_framebuffer_snapshot(BOOT_START_INFO *info,
    const BOOT_MEMORY_MAP *map, const OS_FRAMEBUFFER *fb, UINT32 bits);
#endif
