#ifndef OS_CPU_PAGING_CAPABILITIES_H
#define OS_CPU_PAGING_CAPABILITIES_H
#include "uefi.h"

// ====== capacita rilevate senza modificare i registri di controllo ====== //

typedef struct {
    UINT32 PhysicalBits;
    UINT32 NxSupported;
    UINT32 Valid;
} BOOT_PAGING_CAPABILITIES;

typedef void (*BOOT_CPUID_QUERY)(UINT32 leaf, UINT32 registers[4]);

EFI_STATUS boot_query_paging_capabilities(BOOT_CPUID_QUERY query, BOOT_PAGING_CAPABILITIES *result);
EFI_STATUS boot_read_paging_capabilities(BOOT_PAGING_CAPABILITIES *result);
#endif
