#ifndef BOOT_KERNEL_FILE_TEST_FIXTURE_H
#define BOOT_KERNEL_FILE_TEST_FIXTURE_H

#include "storage/kernel_file.h"
#include "kernel_memory_mock.h"

void make_kernel_fixture(UINT8 *buffer);
static EFI_BOOT_SERVICES_PREFIX services;
static EFI_LOADED_IMAGE_PREFIX loaded;
static EFI_SIMPLE_FILE_SYSTEM filesystem;
static EFI_FILE_PREFIX root, file, probe;
static EFI_SYSTEM_TABLE table;
static EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL console;
static UINT8 data[128 * 1024], pool[128 * 1024];
static UINT8 map_pool[2048];
static UINTN preparation_failures;
static UINTN map_retry_releases;
static int image_token, device_token, mock_error;
static UINTN position, chunk, allocations, frees, file_closes, root_closes;
static UINTN actual_size, reported_size, max_request, probe_position;
static UINTN success_messages, failure_messages, sections;
static BOOT_KERNEL_IMAGE validation_owner;
static int validation_cleanup_error;

// ====== gestione protocolli e apertura ====== //

static EFI_STATUS EFIAPI handle_protocol(EFI_HANDLE handle, EFI_GUID *guid, void **result)
{
    if (handle == &image_token && guid->Data1 == 0x5b1b31a1)
        *result = &loaded;
    else if (handle == &device_token && guid->Data1 == 0x964e5b22)
        *result = &filesystem;
    else
        return EFI_INVALID_PARAMETER;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI open_volume(EFI_SIMPLE_FILE_SYSTEM *self, EFI_FILE_PREFIX **result)
{
    if (self != &filesystem)
        return EFI_INVALID_PARAMETER;
    if (kernel_mock_scenario == 18)
        return EFI_DEVICE_ERROR;
    *result = &root;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI open_file(EFI_FILE_PREFIX *self, EFI_FILE_PREFIX **result,
                                  CHAR16 *path, UINT64 mode, UINT64 attributes)
{
    static CHAR16 expected[] = L"\\EFI\\BOOT\\kernel.elf";
    UINTN i;
    if (self != &root || mode != 1 || attributes != 0)
        return EFI_INVALID_PARAMETER;
    if (path[10] == 'b') {
        *result = &probe;
        return EFI_SUCCESS;
    }
    for (i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i)
        if (path[i] != expected[i])
            return EFI_INVALID_PARAMETER;
    if (kernel_mock_scenario == 1) {
        *result = &file;
        return EFI_NOT_FOUND;
    }
    *result = &file;
    return EFI_SUCCESS;
}

// ====== metadati e allocazione simulati ====== //

static EFI_STATUS EFIAPI get_info(EFI_FILE_PREFIX *self, EFI_GUID *guid, UINTN *size, void *buffer)
{
    UINTN i;
    UINT8 *bytes = (UINT8 *)buffer;
    if (self != &file || guid->Data1 != 0x09576e92 || *size < 82)
        return EFI_INVALID_PARAMETER;
    if (kernel_mock_scenario == 2)
        return EFI_DEVICE_ERROR;
    if (kernel_mock_scenario == 3) {
        *size = 1024;
        return EFI_BUFFER_TOO_SMALL;
    }
    for (i = 0; i < 82; ++i)
        bytes[i] = 0;
    ((UINT64 *)buffer)[0] = 82;
    ((UINT64 *)buffer)[1] = reported_size;
    ((UINT64 *)buffer)[9] = kernel_mock_scenario == 13 ? 16 : 0;
    *size = kernel_mock_scenario == 12 ? 16 : 82;
    if (kernel_mock_scenario == 17)
        bytes[80] = 'X';
    if (kernel_mock_scenario == 21)
        ((UINT64 *)buffer)[0] = 1024;
    if (kernel_mock_scenario == 22)
        *size = 513;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI allocate_pool(UINT32 type, UINTN size, void **result)
{
    if (type == 2 && size == (kernel_mock_page_allocations + 8 + (kernel_mock_scenario == 72 ? 1U : 0U)) * 48 &&
        size <= sizeof(map_pool)) {
        *result = map_pool;
        return EFI_SUCCESS;
    }
    ++allocations;
    if (type != 2 || size != reported_size || size > sizeof(pool)) {
        mock_error = 1;
        return EFI_INVALID_PARAMETER;
    }
    *result = pool;
    if (kernel_mock_scenario == 5)
        return EFI_OUT_OF_RESOURCES;
    if (kernel_mock_scenario == 14)
        *result = 0;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI free_pool(void *buffer)
{
    if (buffer == map_pool) {
        if (kernel_mock_scenario == 37 && kernel_mock_page_allocations > 3 && ++map_retry_releases == 1)
            return EFI_DEVICE_ERROR;
        return EFI_SUCCESS;
    }
    ++frees;
    if (buffer != pool)
        mock_error = 1;
    return kernel_mock_scenario == 9 ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}

// ====== lettura e chiusura simulate ====== //

static EFI_STATUS EFIAPI read_file(EFI_FILE_PREFIX *self, UINTN *size, void *buffer)
{
    UINTN count, i;
    if (self != &file)
        return EFI_INVALID_PARAMETER;
    if (*size > max_request)
        max_request = *size;
    if (kernel_mock_scenario == 6)
        return EFI_DEVICE_ERROR;
    if (kernel_mock_scenario == 15) {
        ++*size;
        return EFI_SUCCESS;
    }
    if (position == actual_size && kernel_mock_scenario == 16) {
        *(UINT8 *)buffer = 'X';
        *size = 1;
        return EFI_SUCCESS;
    }
    count = actual_size - position;
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

static EFI_STATUS EFIAPI read_probe(EFI_FILE_PREFIX *self, UINTN *size, void *buffer)
{
    static const UINT8 contents[] = "OS boot check\r\n";
    UINTN count = sizeof(contents) - 1 - probe_position;
    UINTN i;
    if (self != &probe)
        return EFI_INVALID_PARAMETER;
    if (count > *size)
        count = *size;
    for (i = 0; i < count; ++i)
        ((UINT8 *)buffer)[i] = contents[probe_position + i];
    probe_position += count;
    *size = count;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI close_file(EFI_FILE_PREFIX *self)
{
    if (self == &file) {
        ++file_closes;
        return kernel_mock_scenario == 10 ? EFI_DEVICE_ERROR : EFI_SUCCESS;
    }
    if (self == &root) {
        ++root_closes;
        return kernel_mock_scenario == 11 ? EFI_DEVICE_ERROR : EFI_SUCCESS;
    }
    return self == &probe ? EFI_SUCCESS : EFI_INVALID_PARAMETER;
}

static EFI_STATUS EFIAPI output(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *self, CHAR16 *text)
{
    if (self != &console)
        return EFI_INVALID_PARAMETER;
    if (text[0] == '#' && text[1] == ' ')
        ++sections;
    if (text[0] == '[' && text[2] == 'F')
        ++failure_messages;
    if (text[0] == 'E' && text[1] == 'L' && text[2] == 'F')
        ++success_messages;
    if (kernel_mock_scenario == 28 && text[0] == 'E' && text[1] == 'L' && text[2] == 'F')
        return EFI_DEVICE_ERROR;
    if (text[0] == 'p' && text[1] == 'r' && text[13] == 'k')
        ++preparation_failures;
    return EFI_SUCCESS;
}

static void prepare(int failure)
{
    if (validation_owner.FileBuffer || validation_owner.Pages.Count) {
        kernel_mock_scenario = 0;
        services.FreePool = free_pool;
        services.FreePages = test_free_pages;
        if (boot_release_kernel(&services, &validation_owner) != EFI_SUCCESS)
            validation_cleanup_error = 1;
    }
    kernel_mock_page_allocations = kernel_mock_page_frees = preparation_failures = 0;
    map_retry_releases = 0;
    kernel_mock_scenario = failure;
    mock_error = 0;
    position = allocations = frees = file_closes = root_closes = max_request = 0;
    probe_position = success_messages = failure_messages = sections = 0;
    chunk = 512;
    actual_size = kernel_mock_scenario == 7 ? 400 : 512;
    reported_size = kernel_mock_scenario == 4 ? ELF_MAX_FILE_BYTES + 1 : 512;
    if (kernel_mock_scenario == 23)
        reported_size = 63;
    make_kernel_fixture(data);
    if (kernel_mock_scenario == 8)
        data[0] = 0;
    if (kernel_mock_scenario == 20)
        data[104] = 1;
    services.Hdr.HeaderSize = sizeof(services);
    services.HandleProtocol = handle_protocol;
    services.AllocatePool = allocate_pool;
    services.FreePool = free_pool;
    services.GetMemoryMap = test_get_memory_map;
    services.AllocatePages = test_allocate_pages;
    services.FreePages = test_free_pages;
    loaded.DeviceHandle = &device_token;
    filesystem.OpenVolume = open_volume;
    root.Open = open_file;
    root.Close = close_file;
    file.Close = close_file;
    file.Read = kernel_mock_scenario == 24 ? 0 : read_file;
    file.GetInfo = kernel_mock_scenario == 19 ? 0 : get_info;
    probe.Read = read_probe;
    probe.Close = close_file;
    table.Hdr.Signature = EFI_SYSTEM_TABLE_SIGNATURE;
    table.Hdr.HeaderSize = sizeof(table);
    table.BootServices = &services;
    console.OutputString = output;
    table.ConOut = &console;
}


#endif
