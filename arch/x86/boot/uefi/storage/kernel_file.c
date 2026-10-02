#include "kernel_file.h"
#include "elf/load.h"
#include "console/log.h"
#include "handoff/segments.h"
#include "kernel_buffer.h"

static EFI_GUID file_info_guid = {
    0x09576e92, 0x6d3f, 0x11d2, {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}
};
static CHAR16 kernel_path[] = L"\\EFI\\BOOT\\kernel.elf";

typedef struct {
    UINT64 Size;
    UINT64 FileSize;
    UINT64 PhysicalSize;
    UINT8 TimeFields[48];
    UINT64 Attribute;
} EFI_FILE_INFO_PREFIX;
typedef char check_file_info_size[(sizeof(EFI_FILE_INFO_PREFIX) == 80) ? 1 : -1];

// ====== dimensione file prima delle allocazioni ====== //

static EFI_STATUS kernel_size(EFI_FILE_PREFIX *file, UINTN *file_size)
{
    union {
        EFI_FILE_INFO_PREFIX prefix;
        UINT8 bytes[512];
    } info;
    UINTN size = sizeof(info);
    UINTN index;
    int terminated = 0;
    EFI_STATUS status;
    if (!file->GetInfo)
        return EFI_UNSUPPORTED;
    status = file->GetInfo(file, &file_info_guid, &size, &info);
    if (status == EFI_BUFFER_TOO_SMALL)
        return EFI_LOAD_ERROR;
    if (status != EFI_SUCCESS)
        return status;
    if (size < 82 || size > sizeof(info) || info.prefix.Size < 82 || info.prefix.Size > size ||
        (info.prefix.Size & 1) || (info.prefix.Attribute & 16))
        return EFI_LOAD_ERROR;
    for (index = 80; index + 1 < info.prefix.Size; index += 2)
        if (!info.bytes[index] && !info.bytes[index + 1]) {
            terminated = 1;
            break;
        }
    if (!terminated || info.prefix.FileSize < 64 || info.prefix.FileSize > ELF_MAX_FILE_BYTES)
        return EFI_LOAD_ERROR;
    *file_size = (UINTN)info.prefix.FileSize;
    return EFI_SUCCESS;
}

// ====== lettura completa con richieste limitate ====== //

static EFI_STATUS read_kernel(EFI_FILE_PREFIX *file, UINT8 *buffer, UINTN size)
{
    UINTN used = 0;
    UINTN requested, count;
    UINT8 extra;
    EFI_STATUS status;
    while (used < size) {
        requested = size - used;
        if (requested > 64 * 1024)
            requested = 64 * 1024;
        count = requested;
        status = file->Read(file, &count, buffer + used);
        if (status != EFI_SUCCESS)
            return status;
        if (!count || count > requested)
            return EFI_LOAD_ERROR;
        used += count;
    }
    count = 1;
    status = file->Read(file, &count, &extra);
    if (status != EFI_SUCCESS)
        return status;
    return count == 0 ? EFI_SUCCESS : EFI_LOAD_ERROR;
}

// ====== verifica kernel con rilascio delle risorse ====== //

EFI_STATUS boot_load_kernel(EFI_HANDLE image, EFI_SYSTEM_TABLE *table, KERNEL_FILE_REPORT *report, BOOT_KERNEL_IMAGE *kernel)
{
    EFI_BOOT_SERVICES_PREFIX *services;
    EFI_FILE_PREFIX *root = 0;
    EFI_FILE_PREFIX *file = 0;
    void *allocated = 0;
    UINTN size = 0;
    EFI_STATUS status, cleanup_status;

    if (!report)
        return EFI_INVALID_PARAMETER;
    report->FileBytes = 0;
    report->Validation = ELF_BAD_ARGUMENT;
    report->Plan.Entry = 0;
    report->Plan.TotalMemoryBytes = 0;
    report->Plan.SegmentCount = 0;
    if (!image || !table || !kernel || kernel->Pages.Count || kernel->Pages.AllocatedBytes ||
        kernel->Description.Signature || kernel->FileBuffer || kernel->FileBufferBytes)
        return EFI_INVALID_PARAMETER;
    services = (EFI_BOOT_SERVICES_PREFIX *)table->BootServices;
    if (!services || services->Hdr.HeaderSize < sizeof(*services) ||
        !services->AllocatePool || !services->FreePool)
        return EFI_UNSUPPORTED;
    status = boot_open_volume(image, table, &root);
    if (status != EFI_SUCCESS)
        return status;
    if (!root->Open) {
        status = EFI_UNSUPPORTED;
        goto cleanup;
    }
    status = root->Open(root, &file, kernel_path, 1, 0);
    if (status != EFI_SUCCESS) {
        file = 0;
        goto cleanup;
    }
    if (!file || !file->Close || !file->Read) {
        status = EFI_UNSUPPORTED;
        goto cleanup;
    }
    status = kernel_size(file, &size);
    if (status != EFI_SUCCESS)
        goto cleanup;
    report->FileBytes = size;
    status = services->AllocatePool(2, size, &allocated);
    if (status != EFI_SUCCESS)
        goto cleanup;
    status = boot_adopt_kernel_file_buffer(table, root, file, report, kernel, allocated, size);
    if (status != EFI_SUCCESS) goto cleanup;
    status = read_kernel(file, (UINT8 *)kernel->FileBuffer, size);
    if (status != EFI_SUCCESS)
        goto cleanup;
    report->Validation = elf_validate((const UINT8 *)kernel->FileBuffer, size, &report->Plan);
    if (report->Validation != ELF_VALID) {
        status = EFI_LOAD_ERROR;
        goto cleanup;
    }
    status = boot_section(table->ConOut, L"preparazione segmenti ELF");
    if (status != EFI_SUCCESS)
        goto cleanup;
    status = elf_stage_segments(services, (const UINT8 *)kernel->FileBuffer, size, &kernel->Pages);
    if (status == EFI_SUCCESS)
        status = boot_describe_segments(&report->Plan, &kernel->Pages, &kernel->Description);
    if (status == EFI_SUCCESS)
        status = boot_log(table->ConOut, BOOT_OK,
            L"Segmenti copiati e BSS azzerata; pagine conservate.");
    else
        boot_log(table->ConOut, BOOT_FAIL, L"preparazione o rilascio segmenti falliti.");

    // ====== conserva il primo errore ====== //

cleanup:
    cleanup_status = boot_release_kernel_file_buffer(services, kernel);
    if (status == EFI_SUCCESS) status = cleanup_status;
    if (file && file->Close) {
        cleanup_status = file->Close(file);
        if (status == EFI_SUCCESS)
            status = cleanup_status;
    }
    cleanup_status = root->Close(root);
    if (status == EFI_SUCCESS)
        status = cleanup_status;
    if (status != EFI_SUCCESS) {
        kernel->Description.Signature = 0;
        report->Plan.Entry = 0;
        report->Plan.TotalMemoryBytes = 0;
        report->Plan.SegmentCount = 0;
    }
    return status;
}

// ====== prova di compatibilita con rilascio immediato ====== //

EFI_STATUS boot_validate_kernel(EFI_HANDLE image, EFI_SYSTEM_TABLE *table, KERNEL_FILE_REPORT *report,
                                BOOT_KERNEL_IMAGE *owner)
{
    EFI_STATUS status, cleanup;
    if (!owner || owner->Pages.Count || owner->Pages.AllocatedBytes || owner->Description.Signature ||
        owner->FileBuffer || owner->FileBufferBytes) return EFI_INVALID_PARAMETER;
    status = boot_load_kernel(image, table, report, owner);
    cleanup = boot_release_kernel(table ? (EFI_BOOT_SERVICES_PREFIX *)table->BootServices : 0, owner);
    if (status == EFI_SUCCESS)
        status = cleanup;
    if (status != EFI_SUCCESS && report) {
        report->Plan.Entry = report->Plan.TotalMemoryBytes = 0;
        report->Plan.SegmentCount = 0;
    }
    return status;
}
