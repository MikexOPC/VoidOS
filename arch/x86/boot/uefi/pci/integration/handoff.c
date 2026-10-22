#include "handoff.h"
#include "../../console/log.h"

// ====== boot_prepare_vgpu ======

EFI_STATUS boot_prepare_vgpu(EFI_SYSTEM_TABLE *table, UINT32 bits,
    BOOT_START_INFO *info, BOOT_PCI_OWNER *owner)
{
    EFI_STATUS status;
    if (!table || !table->ConOut || !owner || !info ||
        info->Version != BOOT_START_VERSION || info->Size != sizeof(*info))
        return EFI_INVALID_PARAMETER;
    status = boot_vgpu_acquire(table,bits,owner);
    if (boot_log_hex(table->ConOut,L"VIRTIO_PCI_DISCOVERY_STATUS: ",status) != EFI_SUCCESS ||
        boot_log_hex(table->ConOut,L"VIRTIO_PCI_DISCOVERY_STAGE: ",owner->Stage) != EFI_SUCCESS ||
        boot_log_hex(table->ConOut,L"VIRTIO_PCI_UNAVAILABLE_BARS: ",owner->UnavailableBars) != EFI_SUCCESS)
        return EFI_DEVICE_ERROR;
    if (owner->Handles || owner->Resources) return EFI_DEVICE_ERROR;
    if (status != EFI_SUCCESS && status != EFI_NOT_FOUND && status != EFI_UNSUPPORTED) return status;
    if (!kernel_vgpu_boot_valid(&owner->Result,bits)) return EFI_DEVICE_ERROR;
    info->Gpu = owner->Result;
    return boot_log(table->ConOut,owner->Valid ? BOOT_OK : BOOT_WARN,
        owner->Valid ? L"VIRTIO_PCI_HANDOFF_PRESENT" : L"VIRTIO_PCI_HANDOFF_ABSENT");
}
