#include "storage/kernel_buffer.h"
#include "paging/plan.h"

void make_kernel_fixture(UINT8 *buffer);
__declspec(align(4096)) static UINT8 segment_pages[2][4096];
__declspec(align(8)) static UINT8 file_bytes[512], content_pool[512];
static EFI_BOOT_SERVICES_PREFIX services;
static EFI_SYSTEM_TABLE table;
static EFI_LOADED_IMAGE_PREFIX loaded;
static EFI_SIMPLE_FILE_SYSTEM filesystem;
static EFI_FILE_PREFIX root, file;
static EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL console;
static EFI_SIMPLE_TEXT_OUTPUT_MODE mode;
static KERNEL_FILE_REPORT report;
static BOOT_KERNEL_IMAGE kernel;
static int image_token, device_token;
static void *allocation_output;
static UINTN reads, pool_allocations, pool_releases, page_allocations, page_releases, position;
static UINT32 pool_live, live_pages, pool_failures, page_failure, invalid_calls;

// ====== protocolli minimi per eseguire il loader reale senza firmware ====== //

static EFI_STATUS EFIAPI handle(EFI_HANDLE image, EFI_GUID *guid, void **result)
{
    if (image == &image_token && guid->Data1 == 0x5b1b31a1) *result = &loaded;
    else if (image == &device_token && guid->Data1 == 0x964e5b22) *result = &filesystem;
    else return EFI_INVALID_PARAMETER;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI volume(EFI_SIMPLE_FILE_SYSTEM *self, EFI_FILE_PREFIX **result)
{
    if (self != &filesystem) return EFI_INVALID_PARAMETER;
    *result = &root;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI open_file(EFI_FILE_PREFIX *self, EFI_FILE_PREFIX **result,
                                  CHAR16 *path, UINT64 access, UINT64 attributes)
{
    if (self != &root || !path || access != 1 || attributes) return EFI_INVALID_PARAMETER;
    *result = &file;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI close_file(EFI_FILE_PREFIX *self)
{
    return self == &root || self == &file ? EFI_SUCCESS : EFI_INVALID_PARAMETER;
}

static EFI_STATUS EFIAPI info(EFI_FILE_PREFIX *self, EFI_GUID *guid, UINTN *bytes, void *buffer)
{
    UINTN index;
    UINT8 *output = (UINT8 *)buffer;
    if (self != &file || guid->Data1 != 0x09576e92 || *bytes < 82) return EFI_INVALID_PARAMETER;
    for (index = 0; index < 82; ++index) output[index] = 0;
    output[0] = 82;
    output[9] = 2;
    *bytes = 82;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI read_file(EFI_FILE_PREFIX *self, UINTN *bytes, void *buffer)
{
    UINTN index, count;
    ++reads;
    if (self != &file || allocation_output != content_pool) { ++invalid_calls; return EFI_DEVICE_ERROR; }
    count = *bytes < sizeof(file_bytes) - position ? *bytes : sizeof(file_bytes) - position;
    for (index = 0; index < count; ++index) ((UINT8 *)buffer)[index] = file_bytes[position + index];
    position += count;
    *bytes = count;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI output(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *self, CHAR16 *text)
{
    return self == &console && text ? EFI_SUCCESS : EFI_INVALID_PARAMETER;
}

// ====== modello di proprieta con fallimenti ripetibili di pool e pagine ====== //

static EFI_STATUS EFIAPI allocate_pool(UINT32 type, UINTN bytes, void **result)
{
    ++pool_allocations;
    if (type != 2 || bytes != 512 || pool_live) { ++invalid_calls; return EFI_DEVICE_ERROR; }
    *result = allocation_output;
    pool_live = allocation_output == content_pool;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI free_pool(void *buffer)
{
    ++pool_releases;
    if (buffer != content_pool || !pool_live) { ++invalid_calls; return EFI_INVALID_PARAMETER; }
    if (pool_failures) { --pool_failures; return EFI_DEVICE_ERROR; }
    pool_live = 0;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI allocate_pages(UINT32 type, UINT32 memory, UINTN pages, UINT64 *address)
{
    if (type || memory != 2 || pages != 1 || page_allocations >= 2) {
        ++invalid_calls;
        return EFI_DEVICE_ERROR;
    }
    *address = (UINT64)(UINTN)segment_pages[page_allocations];
    live_pages |= 1U << (UINT32)page_allocations++;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI free_pages(UINT64 address, UINTN pages)
{
    UINT32 index;
    ++page_releases;
    for (index = 0; index < 2; ++index) {
        if (address != (UINT64)(UINTN)segment_pages[index]) continue;
        if (pages != 1 || !(live_pages & (1U << index))) break;
        if (page_failure && index == 1) return EFI_DEVICE_ERROR;
        live_pages &= ~(1U << index);
        return EFI_SUCCESS;
    }
    ++invalid_calls;
    return EFI_INVALID_PARAMETER;
}

// ====== reset consentito soltanto dopo il rilascio delle risorse del modello ====== //

static int prepare(void)
{
    BOOT_KERNEL_IMAGE empty = {0};
    if (pool_live || live_pages || kernel.FileBuffer || kernel.Pages.Count) return 0;
    kernel = empty;
    make_kernel_fixture(file_bytes);
    reads = pool_allocations = pool_releases = page_allocations = page_releases = position = 0;
    pool_failures = page_failure = invalid_calls = 0;
    allocation_output = content_pool;
    services.Hdr.HeaderSize = sizeof(services);
    services.HandleProtocol = handle;
    services.AllocatePool = allocate_pool;
    services.FreePool = free_pool;
    services.AllocatePages = allocate_pages;
    services.FreePages = free_pages;
    loaded.DeviceHandle = &device_token;
    filesystem.OpenVolume = volume;
    root.Open = open_file;
    root.Close = close_file;
    file.GetInfo = info;
    file.Read = read_file;
    file.Close = close_file;
    console.OutputString = output;
    console.Mode = &mode;
    table.BootServices = &services;
    table.ConOut = &console;
    return 1;
}

// ====== buffer rifiutati prima di Read e FreePool anche su alias parziali ====== //

static int check_bad_outputs(void)
{
    void *targets[] = {0, (void *)(UINTN)(~0ULL - 7), content_pool + 1,
        &report, &kernel, &table, &services, &root, &file, &console, &mode,
        (UINT8 *)&report + sizeof(report) - 8,
        (UINT8 *)&kernel + sizeof(kernel) - 8};
    UINTN index;
    for (index = 0; index < sizeof(targets) / sizeof(targets[0]); ++index) {
        EFI_STATUS expected = index ? EFI_DEVICE_ERROR : EFI_OUT_OF_RESOURCES;
        if (!prepare()) return 1;
        allocation_output = targets[index];
        if (boot_load_kernel(&image_token, &table, &report, &kernel) != expected ||
            pool_allocations != 1 || reads || pool_releases || page_allocations ||
            kernel.FileBuffer || kernel.FileBufferBytes || kernel.Pages.Count || invalid_calls) return 2 + (int)index;
        if (boot_release_kernel(&services, &kernel) != EFI_SUCCESS || pool_releases || page_releases) return 20;
    }
    return 0;
}

// ====== conservazione del buffer e cleanup combinato senza doppio rilascio ====== //

__declspec(dllexport) int run_kernel_buffer_tests(void)
{
    BOOT_KERNEL_STACK stack = {0};
    BOOT_MAPPING_PLAN plan;
    UINTN before;
    int result = check_bad_outputs();
    if (result) return 100 + result;
    if (!prepare()) return 200;
    pool_failures = 2;
    if (boot_load_kernel(&image_token, &table, &report, &kernel) != EFI_DEVICE_ERROR ||
        kernel.FileBuffer != content_pool || kernel.FileBufferBytes != 512 ||
        !pool_live || kernel.Pages.Count != 2 || kernel.Description.Signature || pool_releases != 1) return 201;
    if (boot_allocate_stack(&services, &kernel, &stack) != EFI_INVALID_PARAMETER || page_allocations != 2 ||
        boot_plan_mappings(&kernel, &stack, &plan) != EFI_INVALID_PARAMETER) return 210;
    before = pool_allocations;
    if (boot_load_kernel(&image_token, &table, &report, &kernel) != EFI_INVALID_PARAMETER ||
        pool_allocations != before || pool_releases != 1) return 202;
    page_failure = 1;
    if (boot_release_kernel(&services, &kernel) != EFI_DEVICE_ERROR ||
        kernel.FileBuffer != content_pool || !pool_live || live_pages != 2 ||
        kernel.Pages.AllocatedBytes != 4096 || pool_releases != 2 || page_releases != 2) return 203;
    page_failure = 0;
    if (boot_release_kernel(&services, &kernel) != EFI_SUCCESS || kernel.FileBuffer || kernel.FileBufferBytes ||
        kernel.Pages.Count || pool_live || live_pages || pool_releases != 3 || page_releases != 3 || invalid_calls) return 204;
    if (boot_release_kernel(&services, &kernel) != EFI_SUCCESS || pool_releases != 3 || page_releases != 3) return 205;
    if (!prepare()) return 206;
    pool_failures = 2;
    if (boot_validate_kernel(&image_token, &table, &report, &kernel) != EFI_DEVICE_ERROR ||
        kernel.FileBuffer != content_pool || !pool_live || live_pages || pool_releases != 2) return 207;
    if (boot_release_kernel(&services, &kernel) != EFI_SUCCESS || pool_live || kernel.FileBuffer || invalid_calls) return 208;
    if (boot_validate_kernel(&image_token, &table, &report, 0) != EFI_INVALID_PARAMETER) return 209;
    return 0;
}
