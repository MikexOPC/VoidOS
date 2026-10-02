#include "exit_services.h"
#include "lib/memory/address_ranges.h"

// ====== uscita con mappa aggiornata e nessuna chiamata di cleanup ====== //

EFI_STATUS boot_exit_firmware_services_checked(EFI_HANDLE image, EFI_BOOT_SERVICES_EXIT_PREFIX *services,
                                              BOOT_MEMORY_MAP *map, BOOT_EXIT_STATE *state,
                                              BOOT_EXIT_SNAPSHOT_CHECK check, void *context)
{
    EFI_STATUS status;
    if (!state || state->Phase != BOOT_SERVICES_AVAILABLE || state->Attempts)
        return EFI_INVALID_PARAMETER;
    if (map && (os_address_ranges_overlap((UINT64)(UINTN)state, sizeof(*state), (UINT64)(UINTN)map, sizeof(*map)) ||
                os_address_ranges_overlap((UINT64)(UINTN)state, sizeof(*state), (UINT64)(UINTN)map->Buffer, map->Capacity)))
        return EFI_INVALID_PARAMETER;
    if (services && os_address_ranges_overlap((UINT64)(UINTN)state, sizeof(*state), (UINT64)(UINTN)services,
        services->Prefix.Hdr.HeaderSize < sizeof(*services) ? sizeof(*services) : services->Prefix.Hdr.HeaderSize))
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
        if (check) {
            BOOT_MEMORY_MAP expected = *map;
            status = check(map, context);
            if (map->Buffer != expected.Buffer || map->Capacity != expected.Capacity ||
                map->Size != expected.Size || map->Key != expected.Key || map->DescriptorSize != expected.DescriptorSize ||
                map->DescriptorVersion != expected.DescriptorVersion || map->Valid != expected.Valid) {
                *map = expected;
                if (status == EFI_SUCCESS) status = EFI_DEVICE_ERROR;
            }
            state->LastStatus = status;
            if (status != EFI_SUCCESS) { boot_forget_map_snapshot(map); return status; }
        }
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

// ====== percorso base senza verifica aggiuntiva del contratto ====== //

EFI_STATUS boot_exit_firmware_services(EFI_HANDLE image, EFI_BOOT_SERVICES_EXIT_PREFIX *services,
                                      BOOT_MEMORY_MAP *map, BOOT_EXIT_STATE *state)
{
    return boot_exit_firmware_services_checked(image, services, map, state, 0, 0);
}
