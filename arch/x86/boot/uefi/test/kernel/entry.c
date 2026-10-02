#include "handoff/protocol/start_info.h"
#include "arch/x86/configs/boot_profile.h"

static volatile BOOT_U64 probe_marker;

// ====== ingresso del kernel diagnostico senza servizi firmware ====== //

BOOT_U64 kernel_probe_entry(const BOOT_START_INFO *info) {
    if (!info) return 1;
    if (info->Signature != BOOT_START_SIGNATURE ||
        info->Version != BOOT_START_VERSION || info->Size != sizeof(*info)) return 2;
    if (info->RequiredFeatures != BOOT_START_FEATURES ||
        info->EnabledFeatures != BOOT_START_FEATURES || info->Reserved != 0) return 3;
    if (info->PageTableRoot == 0 || (info->PageTableRoot & 4095) != 0 ||
        info->PageTableRoot >= (1ULL << 52)) return 4;
    if (info->StackBase != X86_BOOT_STACK_VIRTUAL || info->StackBytes != X86_BOOT_STACK_BYTES) return 5;
    if (info->KernelEntry < X86_BOOT_PAGE_BYTES || info->KernelEntry >= X86_BOOT_LOW_ADDRESS_LIMIT) return 6;
    if (info->MemoryMapVirtual != X86_BOOT_MAP_VIRTUAL || !info->MemoryMapBytes ||
        !info->MemoryMapCapacity || info->MemoryMapCapacity > X86_BOOT_MAP_MAX_BYTES ||
        (info->MemoryMapCapacity & 4095) || info->MemoryMapBytes > info->MemoryMapCapacity ||
        info->MemoryDescriptorBytes < 40 || info->MemoryDescriptorBytes > 4096 ||
        info->MemoryMapBytes % info->MemoryDescriptorBytes ||
        info->MemoryMapBytes / info->MemoryDescriptorBytes > 4096 ||
        info->MemoryDescriptorVersion != 1 || info->MemoryReserved) return 7;
#if BOOT_PROBE_FAULT == 1
    *(volatile BOOT_U64 *)(void *)info = 0;
#elif BOOT_PROBE_FAULT == 2
    probe_marker = *(volatile BOOT_U64 *)(info->StackBase - 4096);
#elif BOOT_PROBE_FAULT == 3
    ((void (*)(void))(info->StackBase))();
#endif
    probe_marker = BOOT_PROBE_SUCCESS;
    return probe_marker;
}

// ====== lettura del risultato diagnostico per i test host ====== //

BOOT_U64 kernel_probe_marker(void) {
    return probe_marker;
}
