#include "resources.h"

// ====== prepare_private ======

static int prepare_private(X86_KERNEL_SEGMENTS *output, BOOT_U32 cpu) {
    if (!output || ((uintptr_t)output & 15) || !cpu || cpu >= 64) return 0;
    BOOT_U64 metadata = X86_AP_METADATA(cpu);
    for (BOOT_U32 byte = 0; byte < sizeof(*output); ++byte) ((unsigned char *)output)[byte] = 0;
    if (!x86_prepare_boot_gdt(&output->Gdt, &output->Pointer, metadata) ||
        !x86_prepare_boot_tss(&output->Task, &output->Descriptor, metadata + X86_AP_TSS_OFFSET,
            X86_AP_STACK_TOP(cpu) - X86_BOOT_EXCEPTION_STACK_BYTES, X86_AP_IST_BASE(cpu, 1))) return 0;
    // Intel SDM 078US 8.7 Figure 8-11: RSP0 offset 4, IST2/3 at offsets 44/52, canonical 64-bit values.
    for (BOOT_U32 stack = 2; stack <= 3; ++stack) {
        BOOT_U64 top = X86_AP_IST_TOP(cpu, stack);
        for (BOOT_U32 byte = 0; byte < 8; ++byte) output->Task.Bytes[36 + (stack - 1) * 8 + byte] = (unsigned char)(top >> (byte * 8));
    }
    // Three segments, the 16-byte TSS descriptor and two user segments. No LGDT/LTR here.
    output->User[0] = X86_USER_DATA_DESCRIPTOR;
    output->User[1] = X86_USER_CODE_DESCRIPTOR;
    output->Pointer.Bytes[0] = X86_KERNEL_GDT_LIMIT;
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
