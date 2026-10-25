#include "handoff/protocol/start_info.h"
#include "arch/x86/configs/layout.h"
#include "elf/validate.h"

BOOT_U64 kernel_probe_entry(const BOOT_START_INFO *info);
BOOT_U64 kernel_probe_marker(void);

// ====== verifica ingresso e rifiuto dei contratti incompatibili ====== //

__declspec(dllexport) int run_kernel_probe_tests(void) {
    BOOT_START_INFO info = {BOOT_START_SIGNATURE, BOOT_START_VERSION, sizeof(BOOT_START_INFO),
        BOOT_START_FEATURES, BOOT_START_FEATURES, 0x400000,
        (X86_LAYOUT_WINDOWS_VIRTUAL + 0x1000ULL), 65536, 0x101000, 0,
        X86_LAYOUT_MAP_VIRTUAL, 96, 4096, 48, 1, 0};
    BOOT_START_INFO bad;
    BOOT_U32 i;
    if (kernel_probe_marker() != 0 || kernel_probe_entry(0) != 1) return 1;
    for (i = 0; i < 22; ++i) {
        bad = info;
        switch (i) {
        case 0: bad.Signature = 0; break;
        case 1: bad.Version = 1; break;
        case 2: bad.Size = 71; break;
        case 3: bad.RequiredFeatures = 0; break;
        case 4: bad.EnabledFeatures = 1; break;
        case 5: bad.Reserved = 1; break;
        case 6: bad.PageTableRoot = 0; break;
        case 7: bad.PageTableRoot = 4097; break;
        case 8: bad.PageTableRoot = 1ULL << 52; break;
        case 9: bad.StackBase = 0; break;
        case 10: bad.StackBytes = 65535; break;
        case 11: bad.KernelEntry = 1ULL << 47; break;
        case 12: bad.MemoryMapVirtual = 0; break;
        case 13: bad.MemoryMapBytes = 0; break;
        case 14: bad.MemoryMapBytes = 4097; break;
        case 15: bad.MemoryMapCapacity = 0; break;
        case 16: bad.MemoryMapCapacity = 4097; break;
        case 17: bad.MemoryMapCapacity = 0x101000; break;
        case 18: bad.MemoryDescriptorBytes = 0; break;
        case 19: bad.MemoryMapBytes = 95; break;
        case 20: bad.MemoryDescriptorVersion = 2; break;
        default: bad.MemoryReserved = 1; break;
        }
        if (kernel_probe_entry(&bad) == BOOT_PROBE_SUCCESS || kernel_probe_marker() != 0)
            return (int)i + 30;
    }
    if (kernel_probe_entry(&info) != BOOT_PROBE_SUCCESS ||
        kernel_probe_marker() != BOOT_PROBE_SUCCESS) return 20;
    return 0;
}

// ====== verifica indipendente dell'ELF generato dalla toolchain ====== //

__declspec(dllexport) int run_kernel_probe_elf(const UINT8 *bytes, UINT64 size) {
    ELF_LOAD_PLAN plan = {0};
    UINT64 prefix;
    if (elf_validate(bytes, size, &plan) != ELF_VALID) return 1;
    if (plan.Entry != 0x100001000ULL || plan.SegmentCount != 3) return 2;
    if (plan.Segments[0].Flags != 5 || plan.Segments[1].Flags != 4 ||
        plan.Segments[2].Flags != 6) return 3;
    if (plan.Segments[2].FileBytes != 0 || plan.Segments[2].MemoryBytes != 8) return 4;
    for (prefix = 0; prefix < 4096; ++prefix) {
        if (elf_validate(bytes, prefix, &plan) == ELF_VALID || plan.SegmentCount != 0)
            return 5;
    }
    if (size == 0 || elf_validate(bytes, size - 1, &plan) == ELF_VALID) return 6;
    return 0;
}
