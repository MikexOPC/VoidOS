#ifndef OS_START_INFO_PAGE_H
#define OS_START_INFO_PAGE_H
#include "prepare.h"

// ====== proprietario della pagina fisica del contratto ====== //

typedef struct {
    UINT64 PhysicalBase;
    UINT64 Pages;
    UINT32 Ready;
} BOOT_START_PAGE;

EFI_STATUS boot_allocate_start_page(EFI_BOOT_SERVICES_PREFIX *services,
                                   const BOOT_MAPPING_PLAN *plan, UINT32 physical_bits,
                                   BOOT_START_PAGE *page);
EFI_STATUS boot_map_start_page(BOOT_MAPPING_PLAN *plan, const BOOT_START_PAGE *page);
EFI_STATUS boot_release_start_page(EFI_BOOT_SERVICES_PREFIX *services, BOOT_START_PAGE *page);
#endif
