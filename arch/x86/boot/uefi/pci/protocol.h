#ifndef BOOT_PCI_PROTOCOL_H
#define BOOT_PCI_PROTOCOL_H
#include "../file_protocol.h"
#include <stddef.h>

typedef struct BOOT_PCI_IO BOOT_PCI_IO;
typedef EFI_STATUS (EFIAPI *BOOT_PCI_READ)(BOOT_PCI_IO *, UINT32, UINT32, UINTN, void *);
typedef EFI_STATUS (EFIAPI *BOOT_PCI_LOCATION)(BOOT_PCI_IO *, UINTN *, UINTN *, UINTN *, UINTN *);
typedef EFI_STATUS (EFIAPI *BOOT_PCI_BAR)(BOOT_PCI_IO *, UINT8, UINT64 *, void **);
typedef EFI_STATUS (EFIAPI *BOOT_PCI_LOCATE)(UINT32, EFI_GUID *, void *, UINTN *, EFI_HANDLE **);

// UEFI2.10 14.4.1: PCI_IO Pci.Read+48, GetLocation+112, GetBarAttributes+128.
struct BOOT_PCI_IO {
    void *UnusedBeforePci[6];
    BOOT_PCI_READ Read;
    void *Write;
    void *UnusedAfterPci[6];
    BOOT_PCI_LOCATION Location;
    void *Attributes;
    BOOT_PCI_BAR Bar;
};

// UEFI2.10 7.1: LocateHandleBuffer is BootServices+312 on x64.
typedef struct {
    EFI_BOOT_SERVICES_PREFIX Prefix;
    void *Intermediate[19];
    BOOT_PCI_LOCATE Locate;
} BOOT_PCI_SERVICES;
_Static_assert(offsetof(BOOT_PCI_IO,Read) == 48, "PCI Read ABI");
_Static_assert(offsetof(BOOT_PCI_IO,Location) == 112, "PCI Location ABI");
_Static_assert(offsetof(BOOT_PCI_IO,Bar) == 128, "PCI BAR ABI");
_Static_assert(offsetof(BOOT_PCI_SERVICES,Locate) == 312, "LocateHandleBuffer ABI");
#endif
