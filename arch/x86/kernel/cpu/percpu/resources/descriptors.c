#include "resources.h"

// ====== prepare_private ======

static int prepare_private(X86_KERNEL_SEGMENTS *output, BOOT_U32 cpu) {
    if (!output || ((uintptr_t)output & 15) || !cpu || cpu >= 64) return 0;
    BOOT_U64 base = X86_AP_BASE + cpu * X86_AP_STRIDE;
    BOOT_U64 metadata = base + 48 * 4096;
    for (BOOT_U32 byte = 0; byte < sizeof(*output); ++byte) ((unsigned char *)output)[byte] = 0;
    if (!x86_prepare_boot_gdt(&output->Gdt, &output->Pointer, metadata) ||
        !x86_prepare_boot_tss(&output->Task, &output->Descriptor, metadata + 48,
            base + 17 * 4096 - 16384, base + 19 * 4096)) return 0;
    // Intel SDM 078US 8.7 Figure 8-11: RSP0 offset 4, IST2/3 at offsets 44/52, canonical 64-bit values.
    for (BOOT_U32 stack = 2; stack <= 3; ++stack) {
        BOOT_U64 top = base + (stack == 2 ? 29ULL : 35ULL) * 4096;
        for (BOOT_U32 byte = 0; byte < 8; ++byte) output->Task.Bytes[36 + (stack - 1) * 8 + byte] = (unsigned char)(top >> (byte * 8));
    }
    // Three segment entries plus the 16-byte available TSS descriptor: limit 40-1. No LGDT/LTR here.
    output->Pointer.Bytes[0] = 39;
    return 1;
}

// ====== x86_ap_descriptor_prepare ======

int x86_ap_descriptor_prepare(X86_KERNEL_SEGMENTS *output, BOOT_U32 cpu) { return prepare_private(output, cpu); }

// ====== x86_ap_descriptor_verify ======

int x86_ap_descriptor_verify(const X86_KERNEL_SEGMENTS *output, BOOT_U32 cpu) {
    X86_KERNEL_SEGMENTS expected;
    if (!output || !prepare_private(&expected, cpu)) return 0;
    for (BOOT_U32 byte = 0; byte < sizeof(expected); ++byte)
        if (((const unsigned char *)output)[byte] != ((const unsigned char *)&expected)[byte]) return 0;
    return 1;
}
