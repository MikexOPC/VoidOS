#ifndef OS_KERNEL_BOOT_CONTRACT_H
#define OS_KERNEL_BOOT_CONTRACT_H
#include "arch/x86/boot/uefi/handoff/protocol/start_info.h"

// ====== prestiti immutabili conservati senza liberare risorse del loader ====== //

typedef struct {
    BOOT_START_INFO Info;
    const unsigned char *MemoryMap;
} KERNEL_BOOT_CONTEXT;

int kernel_validate_contract(const BOOT_START_INFO *info, BOOT_U64 root, BOOT_U64 entry);
int kernel_capture_contract(const BOOT_START_INFO *info, BOOT_U64 root, BOOT_U64 entry,KERNEL_BOOT_CONTEXT *context);

#endif
