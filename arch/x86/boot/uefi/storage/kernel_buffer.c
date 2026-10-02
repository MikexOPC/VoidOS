#include "kernel_buffer.h"
#include "lib/memory/buffer_capacity.h"
#include "lib/memory/address_ranges.h"

// ====== nessuna lettura scrittura o rilascio su intervalli ambigui ====== //

EFI_STATUS boot_adopt_kernel_file_buffer(EFI_SYSTEM_TABLE *table, EFI_FILE_PREFIX *root,
                                        EFI_FILE_PREFIX *file, const KERNEL_FILE_REPORT *report,
                                        BOOT_KERNEL_IMAGE *kernel, void *buffer, UINTN bytes)
{
    EFI_BOOT_SERVICES_PREFIX *services;
    UINT64 base = (UINT64)(UINTN)buffer;
    if (!table || !root || !file || !report || !kernel || kernel->FileBuffer || kernel->FileBufferBytes)
        return EFI_INVALID_PARAMETER;
    services = (EFI_BOOT_SERVICES_PREFIX *)table->BootServices;
    if (!services || services->Hdr.HeaderSize < sizeof(*services)) return EFI_UNSUPPORTED;
    if (!buffer) return EFI_OUT_OF_RESOURCES;
    if (!os_buffer_extent_is_valid(base, bytes, ELF_MAX_FILE_BYTES) || (base & 7))
        return EFI_DEVICE_ERROR;
    if (os_address_ranges_overlap(base, bytes, (UINT64)(UINTN)kernel, sizeof(*kernel)) ||
        os_address_ranges_overlap(base, bytes, (UINT64)(UINTN)report, sizeof(*report)) ||
        os_address_ranges_overlap(base, bytes, (UINT64)(UINTN)table, sizeof(*table)) ||
        os_address_ranges_overlap(base, bytes, (UINT64)(UINTN)services, services->Hdr.HeaderSize) ||
        os_address_ranges_overlap(base, bytes, (UINT64)(UINTN)root, sizeof(*root)) ||
        os_address_ranges_overlap(base, bytes, (UINT64)(UINTN)file, sizeof(*file)) ||
        (table->ConOut && os_address_ranges_overlap(base, bytes, (UINT64)(UINTN)table->ConOut, sizeof(*table->ConOut))) ||
        (table->ConOut && table->ConOut->Mode &&
         os_address_ranges_overlap(base, bytes, (UINT64)(UINTN)table->ConOut->Mode, sizeof(*table->ConOut->Mode))))
        return EFI_DEVICE_ERROR;
    /* Solo un intervallo distinto e valido diventa una risorsa da liberare. */
    kernel->FileBuffer = buffer;
    kernel->FileBufferBytes = bytes;
    return EFI_SUCCESS;
}
