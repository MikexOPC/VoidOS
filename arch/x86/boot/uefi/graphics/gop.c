#include "gop.h"
static EFI_GUID gop_guid = {
    0x9042a9de,0x23dc,0x4a38,{0x96,0xfb,0x7a,0xde,0xd0,0x80,0x51,0x6a}
};

// ====== boot_gop_release_pending ======

EFI_STATUS boot_gop_release_pending(BOOT_GOP_OWNER *owner)
{
    EFI_STATUS status;
    if (!owner || !owner->Services || !owner->Services->Prefix.FreePool) return EFI_INVALID_PARAMETER;
    if (!owner->Pending) return EFI_SUCCESS;
    status = owner->Services->Prefix.FreePool(owner->Pending);
    if (status != EFI_SUCCESS) return status;
    owner->Pending = 0;
    ++owner->Released;
    return EFI_SUCCESS;
}

// ====== mode_info_valid ======

static int mode_info_valid(const BOOT_GOP_INFO *info, UINTN size)
{
    return info && size >= sizeof(*info) && size <= 4096 && !info->Version;
}

// ====== restore_original ======

static EFI_STATUS restore_original(BOOT_GOP_OWNER *owner)
{
    /* UEFI2.10 section12.9.2.2: SetMode(original) may fail too.
       Dirty remains set until original index is actually observed. */
    if (owner->Protocol->Set(owner->Protocol,owner->Original) != EFI_SUCCESS ||
        !owner->Protocol->Mode || owner->Protocol->Mode->Index != owner->Original) {
        owner->RestoreFailed = 1;
        return EFI_DEVICE_ERROR;
    }
    owner->Dirty = 0;
    owner->RestoreFailed = 0;
    return EFI_DEVICE_ERROR;
}

// ====== boot_gop_abandon ======

EFI_STATUS boot_gop_abandon(BOOT_GOP_OWNER *owner)
{
    if (!owner) return EFI_INVALID_PARAMETER;
    if (owner->Pending) return EFI_INVALID_PARAMETER;
    if (!owner->Valid && !owner->Dirty) return EFI_SUCCESS;
    if (!owner->Protocol || !owner->Protocol->Set) return EFI_INVALID_PARAMETER;
    owner->Dirty = 1;
    (void)restore_original(owner);
    if (owner->Dirty) return EFI_DEVICE_ERROR;
    owner->Valid = 0;
    owner->Framebuffer = (OS_FRAMEBUFFER){0};
    return EFI_SUCCESS;
}

// ====== boot_gop_choose ======

EFI_STATUS boot_gop_choose(EFI_SYSTEM_TABLE *table, UINT32 bits, BOOT_GOP_OWNER *owner)
{
    EFI_STATUS status;
    void *interface = 0;
    BOOT_GOP_SERVICES *services;
    BOOT_GOP_INFO selected = {0};
    UINT32 count, index, best = ~0U, score = ~0U;
    if (!owner || owner->Attempted || owner->Services || owner->Protocol || owner->Pending ||
        owner->Valid || owner->Dirty || owner->Queries || owner->Released || owner->RestoreFailed ||
        owner->Original || owner->Selected || owner->Framebuffer.Base || owner->Framebuffer.Capacity ||
        owner->Framebuffer.Used || owner->Framebuffer.Width || owner->Framebuffer.Height ||
        owner->Framebuffer.Stride || owner->Framebuffer.Format ||
        !table || bits < 32 || bits > 52 || table->Hdr.Signature != EFI_SYSTEM_TABLE_SIGNATURE ||
        table->Hdr.HeaderSize < sizeof(*table)) return EFI_INVALID_PARAMETER;
    services = table->BootServices;
    /* EDK2 stable202508 UefiSpec.h: BOOTSERV signature and table order.
       Check the complete prefix before dereferencing LocateProtocol. */
    if (!services || services->Prefix.Hdr.Signature != 0x56524553544f4f42ULL ||
        services->Prefix.Hdr.HeaderSize < sizeof(*services) ||
        !services->Locate || !services->Prefix.FreePool) return EFI_UNSUPPORTED;
    owner->Services = services;
    owner->Attempted = 1;
    status = services->Locate(&gop_guid,0,&interface);
    if (status != EFI_SUCCESS) return status;
    owner->Protocol = interface;
    if (!owner->Protocol || !owner->Protocol->Query || !owner->Protocol->Set ||
        !owner->Protocol->Mode) return EFI_DEVICE_ERROR;
    count = owner->Protocol->Mode->MaxMode;
    owner->Original = owner->Protocol->Mode->Index;
    if (!count || count > 256 || owner->Original >= count) return EFI_UNSUPPORTED;
    for (index = 0; index < count; ++index) {
        BOOT_GOP_INFO *info = 0;
        UINTN size = 0;
        UINT32 candidate = ~0U;
        int malformed;
        ++owner->Queries;
        /* QueryMode output ownership exists only on EFI_SUCCESS. On error
           outputs are not adopted, read or freed as a fabricated resource. */
        status = owner->Protocol->Query(owner->Protocol,index,&size,&info);
        if (status != EFI_SUCCESS) return status;
        /* A buffer aliasing live mode metadata is not an owned allocation.
           Suspicious firmware: do not free or overwrite that live object. */
        if (!info || !owner->Protocol->Mode || info == owner->Protocol->Mode->Info)
            return EFI_DEVICE_ERROR;
        owner->Pending = info;
        malformed = size < sizeof(*info) || size > 4096;
        if (mode_info_valid(info,size))
            candidate = fb_mode_score(info->Width,info->Height,info->Stride,info->Format);
        if (candidate < score) { selected = *info; score = candidate; best = index; }
        if (boot_gop_release_pending(owner) != EFI_SUCCESS) return EFI_DEVICE_ERROR;
        if (malformed) return EFI_DEVICE_ERROR;
    }
    if (best == ~0U) return EFI_UNSUPPORTED;
    owner->Selected = best;
    owner->Dirty = 1;
    /* SetMode is always called, including selected==original. No index guess.
       Query buffers have already been returned to firmware before this call. */
    status = owner->Protocol->Set(owner->Protocol,best);
    if (status != EFI_SUCCESS) return restore_original(owner);
    {
        BOOT_GOP_MODE *mode = owner->Protocol->Mode;
        BOOT_GOP_INFO *info = mode ? mode->Info : 0;
        UINT64 used;
        if (!mode || mode->MaxMode != count || mode->Index != best ||
            !mode_info_valid(info,mode->SizeOfInfo) || info->Width != selected.Width ||
            info->Height != selected.Height || info->Stride != selected.Stride ||
            info->Format != selected.Format) return restore_original(owner);
        used = fb_descriptor_bytes(mode->Base,mode->Capacity,info->Width,info->Height,
            info->Stride,info->Format,bits);
        if (!used) return restore_original(owner);
        owner->Framebuffer = (OS_FRAMEBUFFER){mode->Base,mode->Capacity,used,
            info->Width,info->Height,info->Stride,info->Format};
    }
    owner->Dirty = 0;
    owner->Valid = 1;
    return EFI_SUCCESS;
}
