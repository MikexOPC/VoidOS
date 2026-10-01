#include "file_protocol.h"

static EFI_BOOT_SERVICES_PREFIX services;
static EFI_LOADED_IMAGE_PREFIX loaded;
static EFI_SIMPLE_FILE_SYSTEM filesystem;
static EFI_FILE_PREFIX root;
static EFI_FILE_PREFIX file;
static EFI_SYSTEM_TABLE table;
static UINTN position, chunk, file_closes, root_closes, protocol_calls;
static int failure;
static UINT8 data[80];
static UINTN data_size;
static int image_token, device_token;
static UINTN console_calls;
static int console_error;
static int console_complete;
static int fail_error_message;

// ====== console per test integrato ====== //

static EFI_STATUS EFIAPI integration_output(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *self, CHAR16 *text)
{
    static CHAR16 success[] = L"# ============ avvio UEFI ============ #\r\n\r\n[  OK  ] avvio UEFI riuscito.\r\n\r\n# ============ verifica file di avvio ============ #\r\n\r\n[ INFO ] Lettura di EFI/BOOT/boot-check.txt\r\n\r\n[  OK  ] lettura file verificata.\r\n\r\n# ============ validazione kernel ============ #\r\n\r\n[ INFO ] Lettura e controllo di EFI/BOOT/kernel.elf\r\n\r\n[ FAIL ] validazione kernel fallita.\r\n\r\n";
    static CHAR16 failed[] = L"# ============ avvio UEFI ============ #\r\n\r\n[  OK  ] avvio UEFI riuscito.\r\n\r\n# ============ verifica file di avvio ============ #\r\n\r\n[ INFO ] Lettura di EFI/BOOT/boot-check.txt\r\n\r\n[ FAIL ] verifica file fallita.\r\n\r\n";
    CHAR16 *expected = failure == 0 ? success : failed;
    UINTN i;
    (void)self;
    if (fail_error_message && text[0] == '[' && text[1] == ' ' && text[2] == 'F') {
        fail_error_message = 2;
        return EFI_DEVICE_ERROR;
    }
    for (i = 0; text[i] != 0; ++i) {
        if (!expected[console_calls] || text[i] != expected[console_calls]) {
            console_error = 1;
            return EFI_DEVICE_ERROR;
        }
        ++console_calls;
    }
    console_complete = expected[console_calls] == 0;
    return EFI_SUCCESS;
}
// ====== protocolli simulati ====== //

static EFI_STATUS EFIAPI handle_protocol(EFI_HANDLE handle, EFI_GUID *guid, void **result)
{
    ++protocol_calls;
    if (failure == 1 || (failure == 2 && protocol_calls == 2))
        return EFI_NOT_FOUND;
    if (protocol_calls == 1) {
        if (handle != &image_token || guid->Data1 != 0x5b1b31a1)
            return EFI_INVALID_PARAMETER;
        *result = &loaded;
    } else {
        if (handle != &device_token || guid->Data1 != 0x964e5b22)
            return EFI_INVALID_PARAMETER;
        *result = &filesystem;
    }
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI open_volume(EFI_SIMPLE_FILE_SYSTEM *self, EFI_FILE_PREFIX **result)
{
    if (self != &filesystem)
        return EFI_INVALID_PARAMETER;
    if (failure == 3)
        return EFI_DEVICE_ERROR;
    *result = &root;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI open_file(EFI_FILE_PREFIX *self, EFI_FILE_PREFIX **result,
                                  CHAR16 *path, UINT64 mode, UINT64 attributes)
{
    static CHAR16 expected[] = L"\\EFI\\BOOT\\boot-check.txt";
    UINTN i;
    if (self != &root || mode != 1 || attributes != 0)
        return EFI_INVALID_PARAMETER;
    for (i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i)
        if (path[i] != expected[i])
            return EFI_INVALID_PARAMETER;
    if (failure == 4)
        return EFI_NOT_FOUND;
    *result = &file;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI read_file(EFI_FILE_PREFIX *self, UINTN *size, void *buffer)
{
    UINTN i, count = data_size - position;
    if (self != &file)
        return EFI_INVALID_PARAMETER;
    if (failure == 5)
        return EFI_DEVICE_ERROR;
    if (failure == 8) {
        ++*size;
        return EFI_SUCCESS;
    }
    if (count > *size)
        count = *size;
    if (count > chunk)
        count = chunk;
    for (i = 0; i < count; ++i)
        ((UINT8 *)buffer)[i] = data[position + i];
    position += count;
    *size = count;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI close_file(EFI_FILE_PREFIX *self)
{
    if (self == &file) {
        ++file_closes;
        return failure == 6 ? EFI_DEVICE_ERROR : EFI_SUCCESS;
    }
    if (self == &root) {
        ++root_closes;
        return failure == 7 ? EFI_DEVICE_ERROR : EFI_SUCCESS;
    }
    return EFI_INVALID_PARAMETER;
}

// ====== preparazione scenario ====== //

static void prepare(int scenario)
{
    static const UINT8 expected[] = "OS boot check\r\n";
    UINTN i;
    failure = scenario;
    position = file_closes = root_closes = protocol_calls = 0;
    chunk = 64;
    data_size = sizeof(expected) - 1;
    for (i = 0; i < data_size; ++i)
        data[i] = expected[i];
    services.Hdr.HeaderSize = sizeof(services);
    services.HandleProtocol = handle_protocol;
    loaded.DeviceHandle = &device_token;
    filesystem.OpenVolume = open_volume;
    root.Open = open_file;
    root.Close = close_file;
    file.Read = read_file;
    file.Close = close_file;
    table.BootServices = &services;
}

// ====== test lettura e rilascio handle ====== //

int run_file_tests(void)
{
    int scenario;
    EFI_STATUS expected_status;
    UINTN expected_file_closes, expected_root_closes;
    UINTN offset;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL console = {0};
    offset = (UINTN)((UINT8 *)&services.HandleProtocol - (UINT8 *)&services);
    if (offset != 152 || sizeof(EFI_GUID) != 16)
        return 101;
    offset = (UINTN)((UINT8 *)&loaded.DeviceHandle - (UINT8 *)&loaded);
    if (offset != 24)
        return 102;
    offset = (UINTN)((UINT8 *)&file.Read - (UINT8 *)&file);
    if (offset != 32)
        return 103;
    for (scenario = 0; scenario <= 8; ++scenario) {
        prepare(scenario);
        expected_status = EFI_SUCCESS;
        if (scenario == 1 || scenario == 2 || scenario == 4)
            expected_status = EFI_NOT_FOUND;
        else if (scenario != 0)
            expected_status = EFI_DEVICE_ERROR;
        expected_file_closes = (scenario == 0 || scenario >= 5) ? 1 : 0;
        expected_root_closes = (scenario == 0 || scenario >= 4) ? 1 : 0;
        if (boot_check_file(&image_token, &table) != expected_status ||
            file_closes != expected_file_closes || root_closes != expected_root_closes)
            return 110 + scenario;
    }
    prepare(0);
    chunk = 3;
    if (boot_check_file(&image_token, &table) != EFI_SUCCESS || position != data_size)
        return 120;
    prepare(0);
    data_size = 0;
    if (boot_check_file(&image_token, &table) != EFI_LOAD_ERROR || file_closes != 1 || root_closes != 1)
        return 121;
    prepare(0);
    data[0] = 'X';
    if (boot_check_file(&image_token, &table) != EFI_LOAD_ERROR)
        return 122;
    prepare(0);
    data_size = sizeof(data);
    if (boot_check_file(&image_token, &table) != EFI_LOAD_ERROR || position != 64)
        return 123;
    prepare(0);
    if (boot_check_file(0, &table) != EFI_INVALID_PARAMETER ||
        boot_check_file(&image_token, 0) != EFI_INVALID_PARAMETER)
        return 124;
    table.BootServices = 0;
    if (boot_check_file(&image_token, &table) != EFI_UNSUPPORTED)
        return 125;
    prepare(0);
    services.Hdr.HeaderSize = sizeof(services) - 1;
    if (boot_check_file(&image_token, &table) != EFI_UNSUPPORTED)
        return 126;
    prepare(0);
    root.Open = 0;
    if (boot_check_file(&image_token, &table) != EFI_UNSUPPORTED || root_closes != 1)
        return 127;
    prepare(0);
    file.Read = 0;
    if (boot_check_file(&image_token, &table) != EFI_UNSUPPORTED || file_closes != 1 || root_closes != 1)
        return 128;
    prepare(0);
    table.Hdr.Signature = EFI_SYSTEM_TABLE_SIGNATURE;
    table.Hdr.HeaderSize = sizeof(table);
    console.OutputString = integration_output;
    table.ConOut = &console;
    console_calls = 0;
    console_error = 0;
    console_complete = 0;
    if (efi_main(&image_token, &table) != EFI_UNSUPPORTED || console_calls == 0 ||
        console_error || !console_complete || file_closes != 1 || root_closes != 1)
        return 129;
    prepare(4);
    console_calls = 0;
    console_error = 0;
    console_complete = 0;
    if (efi_main(&image_token, &table) != EFI_NOT_FOUND || console_calls == 0 ||
        console_error || !console_complete || file_closes != 0 || root_closes != 1)
        return 130;
    prepare(4);
    console_calls = 0;
    console_error = 0;
    fail_error_message = 1;
    if (efi_main(&image_token, &table) != EFI_NOT_FOUND || fail_error_message != 2 ||
        file_closes != 0 || root_closes != 1)
        return 131;
    fail_error_message = 0;
    return 0;
}

