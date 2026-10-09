#include "handoff.h"
#include "lib/memory/address_ranges.h"

// ====== boot_validate_framebuffer_snapshot ======

EFI_STATUS boot_validate_framebuffer_snapshot(const BOOT_START_INFO *info,
    const BOOT_MEMORY_MAP *map, UINT32 bits)
{
    EFI_STATUS status;
    const OS_FRAMEBUFFER *fb;
    UINT64 base, bytes;
    if (!info || !map || !fb_handoff_valid(&info->Framebuffer,bits)) return EFI_INVALID_PARAMETER;
    status = boot_validate_memory_map(map);
    if (status != EFI_SUCCESS) return status;
    fb = &info->Framebuffer;
    if (!fb->Base) return EFI_SUCCESS;
    base = fb->Base & ~4095ULL;
    bytes = ((fb->Base + fb->Capacity + 4095) & ~4095ULL) - base;
    if (os_address_ranges_overlap(base,bytes,(UINT64)(UINTN)info,sizeof(*info)) ||
        os_address_ranges_overlap(base,bytes,(UINT64)(UINTN)map,sizeof(*map)) ||
        os_address_ranges_overlap(base,bytes,(UINT64)(UINTN)map->Buffer,map->Capacity))
        return EFI_INVALID_PARAMETER;
    return fb_handoff_memory_valid(fb,map->Buffer,map->Size,map->DescriptorSize,bits) ?
        EFI_SUCCESS : EFI_LOAD_ERROR;
}

// ====== boot_attach_framebuffer_snapshot ======

EFI_STATUS boot_attach_framebuffer_snapshot(BOOT_START_INFO *info,
    const BOOT_MEMORY_MAP *map, const OS_FRAMEBUFFER *fb, UINT32 bits)
{
    OS_FRAMEBUFFER previous;
    EFI_STATUS status;
    if (!info || !map || !fb || !fb_handoff_valid(fb,bits) ||
        info->Signature != BOOT_START_SIGNATURE || info->Version != BOOT_START_VERSION ||
        info->Size != sizeof(*info) || info->EnabledFeatures ||
        info->RequiredFeatures != BOOT_START_FEATURES ||
        os_address_ranges_overlap((UINT64)(UINTN)info,sizeof(*info),(UINT64)(UINTN)fb,sizeof(*fb)) ||
        os_address_ranges_overlap((UINT64)(UINTN)info,sizeof(*info),(UINT64)(UINTN)map,sizeof(*map)) ||
        os_address_ranges_overlap((UINT64)(UINTN)info,sizeof(*info),(UINT64)(UINTN)map->Buffer,map->Capacity))
        return EFI_INVALID_PARAMETER;
    previous = info->Framebuffer;
    info->Framebuffer = *fb;
    status = boot_validate_framebuffer_snapshot(info,map,bits);
    if (status != EFI_SUCCESS) info->Framebuffer = previous;
    return status;
}
