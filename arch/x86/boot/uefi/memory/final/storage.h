#ifndef OS_FINAL_MEMORY_MAP_STORAGE_H
#define OS_FINAL_MEMORY_MAP_STORAGE_H
#include "refresh.h"
#include "paging/tables.h"

// ====== proprietario delle pagine e fotografia presa in prestito ====== //

typedef struct {
    BOOT_MEMORY_MAP Snapshot;
    UINTN Pages;
} BOOT_FINAL_MEMORY_MAP;

EFI_STATUS boot_allocate_final_map(EFI_BOOT_SERVICES_PREFIX *services,
                                  const BOOT_MAPPING_PLAN *plan,
                                  const BOOT_PAGE_TABLES *tables, UINT32 physical_bits,
                                  BOOT_FINAL_MEMORY_MAP *owner);
EFI_STATUS boot_refresh_final_map(EFI_BOOT_SERVICES_PREFIX *services, BOOT_FINAL_MEMORY_MAP *owner);
EFI_STATUS boot_release_final_map(EFI_BOOT_SERVICES_PREFIX *services, BOOT_FINAL_MEMORY_MAP *owner);
#endif
