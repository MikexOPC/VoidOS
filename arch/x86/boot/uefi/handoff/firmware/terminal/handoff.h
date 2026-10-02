#ifndef OS_TERMINAL_FIRMWARE_HANDOFF_H
#define OS_TERMINAL_FIRMWARE_HANDOFF_H
#include "handoff/firmware/exit_services.h"
#include "memory/final/mapping.h"
#include "handoff/descriptors/transition/storage.h"

// ====== prestiti delle risorse gia preparate per il passaggio ====== //

typedef struct BOOT_EXIT_HANDOFF {
    const BOOT_KERNEL_IMAGE *Kernel;
    const BOOT_KERNEL_STACK *Stack;
    const BOOT_PAGING_CAPABILITIES *Capabilities;
    const BOOT_PAGE_TABLES *Tables;
    BOOT_START_PAGE *Page;
    BOOT_FINAL_MEMORY_MAP *Memory;
    const BOOT_DESCRIPTOR_STORAGE *Descriptors;
} BOOT_EXIT_HANDOFF;

EFI_STATUS boot_validate_exit_handoff(const BOOT_EXIT_HANDOFF *handoff);
EFI_STATUS boot_check_exit_resources(const BOOT_EXIT_HANDOFF *handoff, BOOT_MAPPING_PLAN *plan);
EFI_STATUS boot_exit_memory_handoff(EFI_HANDLE image, EFI_BOOT_SERVICES_EXIT_PREFIX *services,
                                   const BOOT_EXIT_HANDOFF *handoff, BOOT_EXIT_STATE *state);
#endif
