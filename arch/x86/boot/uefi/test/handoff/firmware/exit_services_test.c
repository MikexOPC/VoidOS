#include "handoff/firmware/exit_services.h"

static UINT8 buffer[256];
static UINT32 scenario, maps, exits, forbidden, closed, trace_size;
static UINT8 trace[16];
static int image_token;

// ====== memoria simulata con chiave diversa per ogni fotografia ====== //

static EFI_STATUS EFIAPI get_map(UINTN *size, void *destination, UINTN *key,
                               UINTN *stride, UINT32 *version)
{
    UINTN byte;
    if (closed || destination != buffer || *size != sizeof(buffer) || trace_size >= sizeof(trace)) {
        ++forbidden;
        return EFI_DEVICE_ERROR;
    }
    trace[trace_size++] = 'M';
    ++maps;
    *stride = 48;
    *version = 1;
    if ((scenario == 6 && maps == 2) || scenario == 10) {
        *size = sizeof(buffer) + 48;
        return EFI_BUFFER_TOO_SMALL;
    }
    if ((scenario == 7 && maps == 2) || scenario == 9) return EFI_DEVICE_ERROR;
    for (byte = 0; byte < 96; ++byte) buffer[byte] = 0;
    buffer[0] = buffer[48] = 2;
    buffer[24] = buffer[72] = 1;
    buffer[57] = 16;
    if (scenario == 8 && maps == 2) *version = 2;
    *size = 96;
    *key = scenario == 11 ? 0 : maps * 10;
    return EFI_SUCCESS;
}

// ====== uscita con simulazione di chiave scaduta e chiusura parziale ====== //

static EFI_STATUS EFIAPI exit_services(EFI_HANDLE image, UINTN key)
{
    if (closed || image != &image_token || key != (scenario == 11 ? 0 : maps * 10) || maps != exits + 1 ||
        trace_size >= sizeof(trace)) {
        ++forbidden;
        return EFI_DEVICE_ERROR;
    }
    trace[trace_size++] = 'E';
    ++exits;
    if (scenario == 3 || (scenario >= 6 && scenario <= 8) ||
        (scenario == 1 && exits == 1) || (scenario == 2 && exits < 3))
        return EFI_INVALID_PARAMETER;
    if (scenario == 4) return EFI_DEVICE_ERROR;
    if (scenario == 5) return EFI_UNSUPPORTED;
    closed = 1;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI forbidden_allocate(UINT32 type, UINTN size, void **result)
{
    (void)type; (void)size; (void)result;
    ++forbidden;
    return EFI_DEVICE_ERROR;
}

static EFI_STATUS EFIAPI forbidden_free(void *allocation)
{
    (void)allocation;
    ++forbidden;
    return EFI_DEVICE_ERROR;
}

// ====== sequenza ABI stato irreversibile e assenza di allocazioni ====== //

__declspec(dllexport) int run_exit_services_tests(void)
{
    EFI_BOOT_SERVICES_EXIT_PREFIX services = {0};
    UINT32 test;
    if ((UINTN)((UINT8 *)&services.ExitBootServices - (UINT8 *)&services) != 232) return 900;
    services.Prefix.Hdr.HeaderSize = sizeof(services);
    services.Prefix.GetMemoryMap = get_map;
    services.Prefix.AllocatePool = forbidden_allocate;
    services.Prefix.FreePool = forbidden_free;
    services.ExitBootServices = exit_services;
    for (test = 0; test < 12; ++test) {
        BOOT_MEMORY_MAP map = {0};
        BOOT_EXIT_STATE state = {0};
        EFI_STATUS expected;
        UINT32 phase, attempts, index;
        int succeeds = test < 3 || test == 11;
        scenario = test;
        maps = exits = forbidden = closed = trace_size = 0;
        map.Buffer = buffer;
        map.Capacity = sizeof(buffer);
        expected = succeeds ? EFI_SUCCESS : test == 3 ? EFI_INVALID_PARAMETER :
                   test == 5 ? EFI_UNSUPPORTED : (test == 6 || test == 10) ? EFI_BUFFER_TOO_SMALL :
                   test == 8 ? EFI_LOAD_ERROR : EFI_DEVICE_ERROR;
        phase = succeeds ? BOOT_SERVICES_EXITED : test >= 9 ? BOOT_SERVICES_AVAILABLE : BOOT_SERVICES_RESTRICTED;
        attempts = succeeds ? (test == 11 ? 1 : test + 1) : test == 3 ? 3 : test >= 9 ? 0 : 1;
        if (boot_exit_firmware_services(&image_token, &services, &map, &state) != expected ||
            state.LastStatus != expected || state.Phase != phase || state.Attempts != attempts ||
            exits != attempts || forbidden || map.Buffer != buffer || map.Capacity != sizeof(buffer))
            return 910 + (int)test;
        if (succeeds) {
            if (map.Valid != 1 || map.Key != (test == 11 ? 0 : maps * 10) || map.Size != 96 || !closed) return 930;
        } else if (map.Valid || map.Key || map.Size) return 931;
        for (index = 0; index < trace_size; ++index)
            if (trace[index] != (index & 1 ? 'E' : 'M')) return 932;
        if (attempts) {
            UINT32 previous_maps = maps;
            if (boot_exit_firmware_services(&image_token, &services, &map, &state) != EFI_INVALID_PARAMETER ||
                maps != previous_maps || exits != attempts || state.Phase != phase || state.LastStatus != expected)
                return 933;
        }
    }
    {
        BOOT_MEMORY_MAP map = {0};
        BOOT_EXIT_STATE state = {0};
        maps = exits = 0;
        map.Buffer = buffer;
        map.Capacity = sizeof(buffer);
        services.Prefix.Hdr.HeaderSize = 232;
        if (boot_exit_firmware_services(&image_token, &services, &map, &state) != EFI_UNSUPPORTED || maps || exits ||
            state.Phase != BOOT_SERVICES_AVAILABLE) return 940;
        services.Prefix.Hdr.HeaderSize = sizeof(services);
        if (boot_exit_firmware_services(0, &services, &map, &state) != EFI_INVALID_PARAMETER || maps || exits ||
            boot_exit_firmware_services(&image_token, &services, &map, 0) != EFI_INVALID_PARAMETER) return 941;
    }
    return 0;
}
