#include "kernel_image.h"
#include "lib/memory/buffer_capacity.h"

// ====== buffer temporaneo posseduto anche dopo un errore di FreePool ====== //

EFI_STATUS boot_release_kernel_file_buffer(EFI_BOOT_SERVICES_PREFIX *services, BOOT_KERNEL_IMAGE *kernel)
{
    EFI_STATUS status;
    if (!kernel) return EFI_INVALID_PARAMETER;
    if (!kernel->FileBuffer)
        return kernel->FileBufferBytes ? EFI_INVALID_PARAMETER : EFI_SUCCESS;
    if (!os_buffer_extent_is_valid((UINT64)(UINTN)kernel->FileBuffer,
                                   kernel->FileBufferBytes, ELF_MAX_FILE_BYTES) ||
        ((UINTN)kernel->FileBuffer & 7)) return EFI_INVALID_PARAMETER;
    if (!services || services->Hdr.HeaderSize < sizeof(*services) || !services->FreePool)
        return EFI_UNSUPPORTED;
    status = services->FreePool(kernel->FileBuffer);
    if (status == EFI_SUCCESS) {
        kernel->FileBuffer = 0;
        kernel->FileBufferBytes = 0;
    }
    return status;
}

// ====== invalidazione del contratto prima di ogni tentativo di rilascio ====== //

EFI_STATUS boot_release_kernel(EFI_BOOT_SERVICES_PREFIX *services, BOOT_KERNEL_IMAGE *kernel)
{
    EFI_STATUS status, pages_status;
    if (!kernel)
        return EFI_INVALID_PARAMETER;
    kernel->Description.Signature = 0;
    kernel->Description.VirtualEntry = 0;
    kernel->Description.SegmentCount = 0;
    status = boot_release_kernel_file_buffer(services, kernel);
    if (!kernel->Pages.Count && !kernel->Pages.AllocatedBytes) return status;
    pages_status = elf_release_segments(services, &kernel->Pages);
    return status == EFI_SUCCESS ? pages_status : status;
}
