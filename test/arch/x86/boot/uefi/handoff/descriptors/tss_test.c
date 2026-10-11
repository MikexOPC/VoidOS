#include "arch/x86/lib/descriptors/tss.h"

// ====== lettura indipendente degli interi little endian ====== //

static uint64_t read64(const uint8_t *bytes)
{
    uint64_t value = 0;
    uint32_t index;
    for (index = 0; index < 8; ++index) value |= (uint64_t)bytes[index] << (index * 8);
    return value;
}

// ====== oracolo dei campi e mutazioni di tutti i bit ====== //

__declspec(dllexport) int run_tss_tests(void)
{
    struct { uint64_t Before; X86_BOOT_TSS Tss; uint64_t After; } guarded;
    X86_TSS_DESCRIPTOR descriptor;
    const uint64_t base = UINT64_C(0x123456789000);
    const uint8_t expected[16] = {103, 0, 0, 0x90, 0x78, 0x89, 0, 0x56, 0x34, 0x12, 0, 0, 0, 0, 0, 0};
    const uint64_t invalid[] = {0, 4097, UINT64_MAX, UINT64_C(1) << 47,
                                (UINT64_C(1) << 47) - 16384};
    uint32_t byte, bit, index;
    guarded.Before = guarded.After = UINT64_C(0x0123456789abcdef);
    if (!x86_prepare_boot_tss(&guarded.Tss, &descriptor, base, 0x10000, 0x20000)) return 1;
    if (read64(guarded.Tss.Bytes + 4) != 0x14000 || read64(guarded.Tss.Bytes + 36) != 0x24000 ||
        guarded.Tss.Bytes[102] != 104 || guarded.Tss.Bytes[103]) return 2;
    for (byte = 0; byte < 104; ++byte)
        if (!(byte >= 4 && byte < 12) && !(byte >= 36 && byte < 44) && byte != 102 &&
            guarded.Tss.Bytes[byte]) return 3;
    for (byte = 0; byte < 16; ++byte)
        if (descriptor.Bytes[byte] != expected[byte]) return 4;
    for (byte = 0; byte < 104; ++byte) {
        for (bit = 0; bit < 8; ++bit) {
            guarded.Tss.Bytes[byte] ^= (uint8_t)(1U << bit);
            if (x86_validate_boot_tss(&guarded.Tss, &descriptor, base, 0x10000, 0x20000)) return 5;
            guarded.Tss.Bytes[byte] ^= (uint8_t)(1U << bit);
        }
    }
    for (byte = 0; byte < 16; ++byte) {
        for (bit = 0; bit < 8; ++bit) {
            descriptor.Bytes[byte] ^= (uint8_t)(1U << bit);
            if (x86_validate_boot_tss(&guarded.Tss, &descriptor, base, 0x10000, 0x20000)) return 6;
            descriptor.Bytes[byte] ^= (uint8_t)(1U << bit);
        }
    }
    for (index = 0; index < sizeof(invalid) / sizeof(invalid[0]); ++index) {
        if (x86_prepare_boot_tss(&guarded.Tss, &descriptor, base, invalid[index], 0x20000) ||
            x86_prepare_boot_tss(&guarded.Tss, &descriptor, base, 0x10000, invalid[index])) return 7;
    }
    if (x86_prepare_boot_tss(&guarded.Tss, &descriptor, 0, 0x10000, 0x20000) ||
        x86_prepare_boot_tss(&guarded.Tss, &descriptor, base + 1, 0x10000, 0x20000) ||
        x86_prepare_boot_tss(&guarded.Tss, &descriptor, 0x10000, 0x10000, 0x20000) ||
        x86_prepare_boot_tss(&guarded.Tss, &descriptor, base, 0x10000, 0x13000) ||
        x86_prepare_boot_tss(0, &descriptor, base, 0x10000, 0x20000) ||
        x86_prepare_boot_tss(&guarded.Tss, 0, base, 0x10000, 0x20000)) return 8;
    for (index = 0; index < 104; ++index)
        if (x86_prepare_boot_tss(&guarded.Tss, (X86_TSS_DESCRIPTOR *)(guarded.Tss.Bytes + index),
                                 base, 0x10000, 0x20000)) return 9;
    if (!x86_validate_boot_tss(&guarded.Tss, &descriptor, base, 0x10000, 0x20000) ||
        guarded.Before != UINT64_C(0x0123456789abcdef) || guarded.After != guarded.Before) return 10;
    return 0;
}
