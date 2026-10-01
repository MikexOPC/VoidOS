#ifndef OS_EXIT_BOOT_PROTOCOL_H
#define OS_EXIT_BOOT_PROTOCOL_H
#include "file_protocol.h"

typedef EFI_STATUS (EFIAPI *EFI_EXIT_BOOT_SERVICES)(EFI_HANDLE, UINTN);

// ====== prefisso esteso fino a ExitBootServices senza cambiare il prefisso file ====== //

typedef struct {
    EFI_BOOT_SERVICES_PREFIX Prefix;
    void *Reserved;
    void *RegisterProtocolNotify;
    void *LocateHandle;
    void *LocateDevicePath;
    void *InstallConfigurationTable;
    void *LoadImage;
    void *StartImage;
    void *Exit;
    void *UnloadImage;
    EFI_EXIT_BOOT_SERVICES ExitBootServices;
} EFI_BOOT_SERVICES_EXIT_PREFIX;

_Static_assert(sizeof(EFI_BOOT_SERVICES_PREFIX) == 160, "Prefisso servizi x64");
_Static_assert(sizeof(EFI_BOOT_SERVICES_EXIT_PREFIX) == 240, "Prefisso uscita x64");
#endif
