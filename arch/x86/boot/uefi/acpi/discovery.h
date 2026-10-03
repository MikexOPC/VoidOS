#ifndef OS_BOOT_ACPI_DISCOVERY_H
#define OS_BOOT_ACPI_DISCOVERY_H
#include "memory/regions.h"
#include "handoff/protocol/page.h"
#include "lib/firmware/acpi/rsdp.h"
typedef struct { EFI_GUID VendorGuid; void *VendorTable; } BOOT_CONFIGURATION_TABLE;
_Static_assert(sizeof(BOOT_CONFIGURATION_TABLE) == 24, "ABI ConfigurationTable UEFI x64");
EFI_STATUS boot_discover_acpi(const EFI_SYSTEM_TABLE *system, const BOOT_MEMORY_MAP *map,
                              UINT32 bits, OS_ACPI_ROOT *output);
EFI_STATUS boot_validate_acpi_snapshot(const BOOT_START_INFO *info, const BOOT_MEMORY_MAP *map, UINT32 bits);
EFI_STATUS boot_attach_acpi_snapshot(BOOT_START_INFO *info, const BOOT_MEMORY_MAP *map,
                                     const OS_ACPI_ROOT *root, UINT32 bits);
EFI_STATUS boot_prepare_acpi_handoff(EFI_SYSTEM_TABLE *system, const BOOT_MEMORY_MAP *map,
                                     UINT32 bits, const BOOT_START_PAGE *page);
#endif
