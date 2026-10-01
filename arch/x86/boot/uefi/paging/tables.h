#ifndef OS_PAGING_TABLES_H
#define OS_PAGING_TABLES_H
#include "plan.h"

#define BOOT_TABLE_LIMIT X86_BOOT_TABLE_LIMIT
#define BOOT_PTE_NX (1ULL << 63)

// ====== proprietario esclusivo delle tabelle inattive ====== //

typedef struct {
    UINT64 Root;
    UINT32 Count;
    UINT32 Valid;
    UINT64 Pages[BOOT_TABLE_LIMIT];
} BOOT_PAGE_TABLES;

EFI_STATUS boot_build_page_tables(EFI_BOOT_SERVICES_PREFIX *services,
                                 const BOOT_MAPPING_PLAN *plan, UINT32 physical_bits,
                                 UINT32 nx_supported, BOOT_PAGE_TABLES *tables);
EFI_STATUS boot_release_page_tables(EFI_BOOT_SERVICES_PREFIX *services, BOOT_PAGE_TABLES *tables);
#endif
