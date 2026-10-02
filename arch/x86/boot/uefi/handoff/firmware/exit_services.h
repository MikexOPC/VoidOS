#ifndef OS_EXIT_BOOT_SERVICES_H
#define OS_EXIT_BOOT_SERVICES_H
#include "exit_protocol.h"
#include "memory/final/refresh.h"

#define BOOT_SERVICES_AVAILABLE 0U
#define BOOT_SERVICES_RESTRICTED 1U
#define BOOT_SERVICES_EXITED 2U
#define BOOT_EXIT_ATTEMPT_LIMIT 3U

// ====== stato esplicito dopo il primo tentativo di uscita ====== //

typedef struct {
    UINT32 Phase;
    UINT32 Attempts;
    EFI_STATUS LastStatus;
} BOOT_EXIT_STATE;

typedef EFI_STATUS (*BOOT_EXIT_SNAPSHOT_CHECK)(const BOOT_MEMORY_MAP *map, void *context);

EFI_STATUS boot_exit_firmware_services_checked(EFI_HANDLE image, EFI_BOOT_SERVICES_EXIT_PREFIX *services,
                                              BOOT_MEMORY_MAP *map, BOOT_EXIT_STATE *state,
                                              BOOT_EXIT_SNAPSHOT_CHECK check, void *context);

EFI_STATUS boot_exit_firmware_services(EFI_HANDLE image, EFI_BOOT_SERVICES_EXIT_PREFIX *services, BOOT_MEMORY_MAP *map, BOOT_EXIT_STATE *state);
#endif
