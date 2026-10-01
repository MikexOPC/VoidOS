#ifndef OS_BOOT_MEMORY_REGIONS_H
#define OS_BOOT_MEMORY_REGIONS_H
#include "map.h"

// ====== descrizione copiata senza riferimenti al buffer firmware ====== //

typedef struct {
    UINT64 PhysicalBase;
    UINT64 Pages;
    UINT64 Attributes;
    UINT32 FirmwareType;
    UINT32 ConventionalCandidate;
} BOOT_MEMORY_REGION;

typedef struct {
    UINT64 ConventionalCandidatePages;
    UINT64 ExcludedPages;
    UINTN RegionCount;
} BOOT_MEMORY_SUMMARY;

EFI_STATUS boot_read_memory_region(const BOOT_MEMORY_MAP *map, UINTN index, BOOT_MEMORY_REGION *region);
EFI_STATUS boot_summarize_memory(const BOOT_MEMORY_MAP *map, BOOT_MEMORY_SUMMARY *summary);
#endif
