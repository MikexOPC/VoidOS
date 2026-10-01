#ifndef OS_VERIFY_TABLE_CONTENTS_H
#define OS_VERIFY_TABLE_CONTENTS_H
#include "paging/tables.h"

EFI_STATUS boot_verify_table_contents(const BOOT_MAPPING_PLAN *plan,
                                     const BOOT_PAGE_TABLES *tables, UINT32 physical_bits);
#endif
