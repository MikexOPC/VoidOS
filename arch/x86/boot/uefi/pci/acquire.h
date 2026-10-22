#ifndef BOOT_PCI_ACQUIRE_H
#define BOOT_PCI_ACQUIRE_H
#include "protocol.h"
#include "kernel/drivers/video/virtio/discovery/handoff.h"

typedef struct {
    BOOT_PCI_SERVICES  *Services;
    EFI_HANDLE  *Handles;
    void  *Resources;
    UINTN  HandleCount;
    UINT32  Attempted, Valid, Stage, UnavailableBars;
    KERNEL_VGPU_BOOT  Result;
} BOOT_PCI_OWNER;
EFI_STATUS boot_vgpu_acquire(EFI_SYSTEM_TABLE *table, UINT32 bits, BOOT_PCI_OWNER *owner);
EFI_STATUS boot_vgpu_release(BOOT_PCI_OWNER *owner);
int boot_vgpu_descriptor(const unsigned char *bytes, UINTN capacity, UINT32 low,
    UINT32 high, UINT32 bits, UINT64 *size);
#endif
