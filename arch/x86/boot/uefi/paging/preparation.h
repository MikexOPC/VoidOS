#ifndef OS_PAGING_PREPARATION_H
#define OS_PAGING_PREPARATION_H
#include "tables.h"
#include "handoff/protocol/page.h"
#include "cpu/paging_capabilities.h"
#include "memory/map.h"

EFI_STATUS boot_check_table_resources(const BOOT_MEMORY_MAP *map,
                                     const BOOT_PAGE_TABLES *tables, UINT32 physical_bits);
EFI_STATUS boot_prepare_page_tables(EFI_BOOT_SERVICES_PREFIX *services,
                                   const BOOT_KERNEL_IMAGE *kernel, const BOOT_KERNEL_STACK *stack,
                                   const BOOT_PAGING_CAPABILITIES *capabilities, BOOT_PAGE_TABLES *tables,
                                   BOOT_MEMORY_MAP *map);
EFI_STATUS boot_prepare_handoff_tables(EFI_BOOT_SERVICES_PREFIX *services,
                                      const BOOT_KERNEL_IMAGE *kernel, const BOOT_KERNEL_STACK *stack,
                                      const BOOT_PAGING_CAPABILITIES *capabilities, BOOT_PAGE_TABLES *tables,
                                      BOOT_MEMORY_MAP *map, BOOT_START_PAGE *page);
#endif
