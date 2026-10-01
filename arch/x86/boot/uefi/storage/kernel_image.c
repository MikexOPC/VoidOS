#include "kernel_image.h"

// ====== invalidazione del contratto prima di ogni tentativo di rilascio ====== //

EFI_STATUS boot_release_kernel(EFI_BOOT_SERVICES_PREFIX *services, BOOT_KERNEL_IMAGE *kernel)
{
    if (!kernel)
        return EFI_INVALID_PARAMETER;
        kernel->Description.Signature = 0;
        kernel->Description.VirtualEntry = 0;
        kernel->Description.SegmentCount = 0;
    if (!kernel->Pages.Count && !kernel->Pages.AllocatedBytes)
        return EFI_SUCCESS;
    return elf_release_segments(services, &kernel->Pages);
}
