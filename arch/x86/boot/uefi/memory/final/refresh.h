#ifndef OS_REFRESH_MEMORY_MAP_H
#define OS_REFRESH_MEMORY_MAP_H
#include "memory/map.h"

void boot_forget_map_snapshot(BOOT_MEMORY_MAP *map);
EFI_STATUS boot_refresh_memory_map(EFI_BOOT_SERVICES_PREFIX *services, BOOT_MEMORY_MAP *map);
#endif
