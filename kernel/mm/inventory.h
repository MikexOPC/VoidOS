#ifndef OS_KERNEL_MEMORY_INVENTORY_H
#define OS_KERNEL_MEMORY_INVENTORY_H
#include "kernel/boot/contract.h"

typedef struct {
    BOOT_U64 ConventionalPages, ReservedPages;
    BOOT_U32 Regions;
} KERNEL_MEMORY_INVENTORY;
typedef struct {
    BOOT_U64 Base, Pages, Attributes;
    BOOT_U32 Type;
} KERNEL_MEMORY_REGION;
int kernel_read_region(const unsigned char *map, BOOT_U64 bytes, BOOT_U64 stride,
                       BOOT_U64 index, KERNEL_MEMORY_REGION *region);
BOOT_U32 kernel_region_valid(BOOT_U64 base, BOOT_U64 pages, BOOT_U64 attributes, BOOT_U32 type);
BOOT_U32 kernel_region_candidate(BOOT_U64 attributes, BOOT_U32 type);
BOOT_U32 kernel_regions_overlap(BOOT_U64 first, BOOT_U64 pages, BOOT_U64 second, BOOT_U64 other_pages);
int kernel_inventory_memory(const unsigned char *map, BOOT_U64 bytes, BOOT_U64 stride,
                            KERNEL_MEMORY_INVENTORY *output);
#endif
