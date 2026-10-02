#ifndef OS_DESCRIPTOR_STORAGE_H
#define OS_DESCRIPTOR_STORAGE_H
#include "uefi.h"
#include "paging/plan.h"
#include "paging/tables.h"
#include "bundle.h"

#define BOOT_DESCRIPTOR_PAGES 9U
struct BOOT_EXIT_HANDOFF;

// ====== un owner per pagina descrittori e due stack fisicamente contigui ====== //

typedef struct {
    UINT64 PhysicalBase;
    UINTN Pages;
    UINT32 Ready;
} BOOT_DESCRIPTOR_STORAGE;

EFI_STATUS boot_allocate_descriptor_storage(EFI_SYSTEM_TABLE *system_table,
    const struct BOOT_EXIT_HANDOFF *handoff, BOOT_DESCRIPTOR_STORAGE *owner);
EFI_STATUS boot_release_descriptor_storage(EFI_BOOT_SERVICES_PREFIX *services, BOOT_DESCRIPTOR_STORAGE *owner);
EFI_STATUS boot_map_descriptor_storage(BOOT_MAPPING_PLAN *plan, const BOOT_DESCRIPTOR_STORAGE *owner);
EFI_STATUS boot_check_descriptor_storage(const BOOT_DESCRIPTOR_STORAGE *owner);
EFI_STATUS boot_prepare_descriptor_handoff(EFI_SYSTEM_TABLE *system_table, struct BOOT_EXIT_HANDOFF *handoff,
                                           BOOT_DESCRIPTOR_STORAGE *owner, BOOT_PAGE_TABLES *tables);
#endif
