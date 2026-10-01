#ifndef OS_PREPARE_FINAL_MEMORY_MAP_H
#define OS_PREPARE_FINAL_MEMORY_MAP_H
#include "memory/final/storage.h"
#include "paging/preparation.h"

// ====== preparazione senza tentare l'uscita dai servizi firmware ====== //

EFI_STATUS boot_prepare_final_memory_map(EFI_BOOT_SERVICES_PREFIX *services,
                                        const BOOT_KERNEL_IMAGE *kernel, const BOOT_KERNEL_STACK *stack,
                                        const BOOT_PAGING_CAPABILITIES *capabilities,
                                        const BOOT_PAGE_TABLES *tables, const BOOT_START_PAGE *page,
                                        BOOT_FINAL_MEMORY_MAP *owner);
#endif
