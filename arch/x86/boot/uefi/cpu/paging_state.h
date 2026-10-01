#ifndef OS_CPU_PAGING_STATE_H
#define OS_CPU_PAGING_STATE_H
#include "paging_capabilities.h"
#include "arch/x86/lib/processor/paging_validation.h"

#define BOOT_PAGING_NXE X86_PAGING_NXE
#define BOOT_PAGING_WP X86_PAGING_WP
#define BOOT_PAGING_PCID X86_PAGING_PCID

// ====== fotografia numerica dei registri senza possederne le pagine ====== //

typedef struct {
    UINT64 Cr0;
    UINT64 Cr3;
    UINT64 Cr4;
    UINT64 Efer;
    UINT64 Root;
    UINT32 Flags;
    UINT32 Valid;
} BOOT_PAGING_STATE;

EFI_STATUS boot_decode_paging_state(const BOOT_PAGING_CAPABILITIES *capabilities, BOOT_PAGING_STATE *state);
EFI_STATUS boot_read_paging_state(const BOOT_PAGING_CAPABILITIES *capabilities, BOOT_PAGING_STATE *state);
EFI_STATUS boot_compare_paging_state(const BOOT_PAGING_STATE *before, const BOOT_PAGING_STATE *after);
#endif
