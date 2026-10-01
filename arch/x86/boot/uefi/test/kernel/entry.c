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
    probe_marker = BOOT_PROBE_SUCCESS;
    return probe_marker;
}

// ====== lettura del risultato diagnostico per i test host ====== //

BOOT_U64 kernel_probe_marker(void) {
    return probe_marker;
}
