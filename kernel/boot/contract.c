#include "contract.h"
#include "arch/x86/configs/boot_profile.h"
#include "kernel/drivers/video/framebuffer/handoff.h"

// ====== formato numerico controllato prima di leggere la mappa ====== //

int kernel_validate_contract(const BOOT_START_INFO *info, BOOT_U64 root, BOOT_U64 entry)
{
    if (!info || info->Signature != BOOT_START_SIGNATURE || info->Version != BOOT_START_VERSION ||
        info->Size != sizeof(*info) || info->Reserved || info->MemoryReserved ||
        !os_acpi_root_valid(&info->Acpi, 52) || !fb_handoff_valid(&info->Framebuffer,52) ||
        !kernel_vgpu_boot_valid(&info->Gpu,52)
    ) return 0;

    if (info->RequiredFeatures != BOOT_START_FEATURES || info->EnabledFeatures != BOOT_START_FEATURES ||
        root < 4096 || (root & 4095) || root >= (1ULL << 52) || info->PageTableRoot != root) return 0;
    if (entry < 4096 || entry >= X86_BOOT_LOW_ADDRESS_LIMIT || info->KernelEntry != entry ||
        info->StackBase != X86_BOOT_STACK_VIRTUAL || info->StackBytes != X86_BOOT_STACK_BYTES) return 0;
    if (info->MemoryMapVirtual != X86_BOOT_MAP_VIRTUAL || !info->MemoryMapBytes ||
        !info->MemoryMapCapacity || info->MemoryMapCapacity > X86_BOOT_MAP_MAX_BYTES ||
        (info->MemoryMapCapacity & 4095) || info->MemoryMapBytes > info->MemoryMapCapacity ||
        info->MemoryDescriptorBytes < 40 || info->MemoryDescriptorBytes > 4096 ||
        info->MemoryMapBytes % info->MemoryDescriptorBytes ||
        info->MemoryMapBytes / info->MemoryDescriptorBytes > 4096 || info->MemoryDescriptorVersion != 1) return 0;
    return 1;
}

// ====== copia del contratto e prestito della mappa ancora posseduta ====== //

int kernel_capture_contract(const BOOT_START_INFO *info, BOOT_U64 root, BOOT_U64 entry, KERNEL_BOOT_CONTEXT *context)
{
    if (!context || !kernel_validate_contract(info, root, entry)) return 0;
    if ((BOOT_U64)info > ~0ULL - sizeof(*info) || (BOOT_U64)context > ~0ULL - sizeof(*context) ||
        ((BOOT_U64)context < (BOOT_U64)info + sizeof(*info) &&
         (BOOT_U64)info < (BOOT_U64)context + sizeof(*context)) ||
        ((BOOT_U64)context < info->MemoryMapVirtual + info->MemoryMapCapacity &&
         info->MemoryMapVirtual < (BOOT_U64)context + sizeof(*context))) return 0;
    {
        BOOT_U32 byte;
        for (byte = 0; byte < sizeof(*info); ++byte)
            ((volatile unsigned char *)&context->Info)[byte] = ((const unsigned char *)info)[byte];
        context->MemoryMap = (const unsigned char *)info->MemoryMapVirtual;
    }
    return 1;
}
