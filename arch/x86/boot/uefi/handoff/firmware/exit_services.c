#include "exit_services.h"

// ====== uscita con mappa aggiornata e nessuna chiamata di cleanup ====== //

EFI_STATUS boot_exit_firmware_services(EFI_HANDLE image, EFI_BOOT_SERVICES_EXIT_PREFIX *services,
                                      BOOT_MEMORY_MAP *map, BOOT_EXIT_STATE *state)
{
    EFI_STATUS status;
    if (!state || state->Phase != BOOT_SERVICES_AVAILABLE || state->Attempts)
        return EFI_INVALID_PARAMETER;
    state->LastStatus = EFI_INVALID_PARAMETER;
    if (!image || !map) return state->LastStatus;
    if (!services || services->Prefix.Hdr.HeaderSize < sizeof(*services) ||
        !services->ExitBootServices || !services->Prefix.GetMemoryMap) {
        state->LastStatus = EFI_UNSUPPORTED;
        return state->LastStatus;
    }
    while (state->Attempts < BOOT_EXIT_ATTEMPT_LIMIT) {
        status = boot_refresh_memory_map(&services->Prefix, map);
        state->LastStatus = status;
        if (status != EFI_SUCCESS) return status;
        state->Phase = BOOT_SERVICES_RESTRICTED;
        ++state->Attempts;
        status = services->ExitBootServices(image, map->Key);
        state->LastStatus = status;
        if (status == EFI_SUCCESS) {
            state->Phase = BOOT_SERVICES_EXITED;
            return EFI_SUCCESS;
        }
        boot_forget_map_snapshot(map);
        if (status != EFI_INVALID_PARAMETER) return status;
    }
    return state->LastStatus;
}
