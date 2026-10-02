#ifndef OS_BOOT_MEMORY_OWNERSHIP_H
#define OS_BOOT_MEMORY_OWNERSHIP_H
#include "regions.h"

EFI_STATUS boot_check_loader_range(const BOOT_MEMORY_MAP *map, UINT64 base, UINTN pages);
EFI_STATUS boot_check_code_range(const BOOT_MEMORY_MAP *map, UINT64 base);
#endif
