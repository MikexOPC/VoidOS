#ifndef OS_FINAL_MEMORY_MAPPING_H
#define OS_FINAL_MEMORY_MAPPING_H
#include "storage.h"
#include "handoff/protocol/page.h"

// ====== mappa persistente visibile nel futuro spazio del kernel ====== //

EFI_STATUS boot_prepare_memory_handoff(EFI_BOOT_SERVICES_PREFIX *services,
                                      const BOOT_KERNEL_IMAGE *kernel, const BOOT_KERNEL_STACK *stack,
                                      const BOOT_PAGING_CAPABILITIES *capabilities,
                                      BOOT_PAGE_TABLES *tables, BOOT_START_PAGE *page,
                                      BOOT_FINAL_MEMORY_MAP *owner);
EFI_STATUS boot_attach_memory_snapshot(BOOT_START_INFO *info, const BOOT_MEMORY_MAP *map);
#endif
