#ifndef OS_BOOT_PREPARE_START_H
#define OS_BOOT_PREPARE_START_H
#include "start_info.h"
#include "paging/tables.h"
#include "cpu/paging_capabilities.h"

// ====== stack numerico per la futura chiamata Microsoft x64 ====== //

typedef struct {
    UINT64 BeforeCall;
    UINT64 AtEntry;
    UINT64 ShadowBytes;
} BOOT_ENTRY_STACK;

EFI_STATUS boot_plan_entry_stack(UINT64 base, UINT64 bytes, BOOT_ENTRY_STACK *stack);
EFI_STATUS boot_prepare_start_info(const BOOT_MAPPING_PLAN *plan,
                                  const BOOT_PAGE_TABLES *tables,
                                  const BOOT_PAGING_CAPABILITIES *capabilities,
                                  BOOT_START_INFO *info);
void boot_invalidate_start_info(BOOT_START_INFO *info);
#endif
