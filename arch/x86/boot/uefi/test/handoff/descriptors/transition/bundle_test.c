#include "handoff/descriptors/transition/bundle.h"

// ====== profil numerico distinto dai buffer accessibili del test ====== //

static void make_layout(BOOT_DESCRIPTOR_LAYOUT *layout)
{
    uint32_t vector;
    layout->LinearBase = 0x100000;
    layout->PrivilegeStack = 0x200000;
    layout->ExceptionStack = 0x300000;
    for (vector = 0; vector < 32; ++vector) layout->Targets[vector] = 0x400000 + vector * 10;
}

// ====== ogni bit dei campi pubblicati deve invalidare il preflight ====== //

static int check_mutations(BOOT_DESCRIPTOR_BUNDLE *bundle, BOOT_DESCRIPTOR_LAYOUT *layout)
{
    const uint32_t offsets[] = {
        BOOT_DESCRIPTOR_OFFSET(bundle, Signature), BOOT_DESCRIPTOR_OFFSET(bundle, Gdt),
        BOOT_DESCRIPTOR_OFFSET(bundle, TaskDescriptor), BOOT_DESCRIPTOR_OFFSET(bundle, Gdtr),
        BOOT_DESCRIPTOR_OFFSET(bundle, Idtr), BOOT_DESCRIPTOR_OFFSET(bundle, Task),
        BOOT_DESCRIPTOR_OFFSET(bundle, Idt), BOOT_DESCRIPTOR_OFFSET(bundle, ReservedBeforeTask),
        BOOT_DESCRIPTOR_OFFSET(bundle, ReservedBeforeIdt)
    };
    const uint32_t lengths[] = {8, 24, 16, 10, 10, 104, 512, 12, 8};
    uint8_t *bytes = (uint8_t *)bundle;
    uint8_t *input = (uint8_t *)layout;
    uint32_t field, byte, bit;
    for (field = 0; field < 9; ++field) {
        for (byte = offsets[field]; byte < offsets[field] + lengths[field]; ++byte) {
            for (bit = 0; bit < 8; ++bit) {
                bytes[byte] ^= (uint8_t)(1U << bit);
                if (boot_validate_descriptor_bundle(bundle, layout)) return 1;
                bytes[byte] ^= (uint8_t)(1U << bit);
            }
        }
    }
    for (byte = 0; byte < sizeof(*layout); ++byte) {
        for (bit = 0; bit < 8; ++bit) {
            input[byte] ^= (uint8_t)(1U << bit);
            if (boot_validate_descriptor_bundle(bundle, layout)) return 2;
            input[byte] ^= (uint8_t)(1U << bit);
        }
    }
    return !boot_validate_descriptor_bundle(bundle, layout) ? 3 : 0;
}

// ====== errori di layout senza pubblicazioni o azzeramenti parziali ====== //

static int check_rejections(BOOT_DESCRIPTOR_BUNDLE *bundle)
{
    BOOT_DESCRIPTOR_LAYOUT layout;
    const uint64_t bad[] = {0, 4097, UINT64_MAX, UINT64_C(1) << 47};
    uint8_t *bytes = (uint8_t *)bundle;
    uint32_t index, byte;
    make_layout(&layout);
    for (byte = 0; byte < sizeof(*bundle); ++byte) bytes[byte] = 0xa5;
    for (index = 0; index < sizeof(bad) / sizeof(bad[0]); ++index) {
        layout.LinearBase = bad[index];
        if (boot_prepare_descriptor_bundle(bundle, &layout)) return 1;
        make_layout(&layout);
        layout.ExceptionStack = bad[index];
        if (boot_prepare_descriptor_bundle(bundle, &layout)) return 2;
        make_layout(&layout);
    }
    layout.ExceptionStack = layout.PrivilegeStack + 4096;
    if (boot_prepare_descriptor_bundle(bundle, &layout)) return 3;
    make_layout(&layout);
    layout.LinearBase = layout.ExceptionStack + 4096;
    if (boot_prepare_descriptor_bundle(bundle, &layout)) return 4;
    make_layout(&layout);
    layout.Targets[31] = layout.LinearBase + 4095;
    if (boot_prepare_descriptor_bundle(bundle, &layout)) return 5;
    layout.Targets[31] = layout.ExceptionStack + 16383;
    if (boot_prepare_descriptor_bundle(bundle, &layout)) return 6;
    layout.Targets[31] = layout.PrivilegeStack;
    if (boot_prepare_descriptor_bundle(bundle, &layout)) return 7;
    for (index = 0; index < sizeof(*bundle); index += 8)
        if (boot_prepare_descriptor_bundle(bundle, (const BOOT_DESCRIPTOR_LAYOUT *)(bytes + index))) return 8;
    if (boot_prepare_descriptor_bundle(0, &layout) || boot_prepare_descriptor_bundle(bundle, 0) ||
        boot_validate_descriptor_bundle(0, &layout) || boot_validate_descriptor_bundle(bundle, 0)) return 9;
    for (byte = 0; byte < sizeof(*bundle); ++byte)
        if (bytes[byte] != 0xa5) return 10;
    return 0;
}

// ====== commit finale dello stato e invalidazione soltanto della firma ====== //

__declspec(dllexport) int run_descriptor_bundle_tests(void)
{
    BOOT_DESCRIPTOR_BUNDLE bundle;
    BOOT_DESCRIPTOR_LAYOUT layout;
    uint8_t snapshot[sizeof(bundle)];
    const uint8_t *bytes = (const uint8_t *)&bundle;
    uint32_t byte;
    int result;
    make_layout(&layout);
    layout.LinearBase = (UINT64_C(1) << 47) - 4096;
    if (!boot_prepare_descriptor_bundle(&bundle, &layout) ||
        !boot_validate_descriptor_bundle(&bundle, &layout)) return 3;
    layout.ExceptionStack = (UINT64_C(1) << 47) - 16384;
    if (boot_prepare_descriptor_bundle(&bundle, &layout)) return 4;
    layout.ExceptionStack -= 4096;
    if (!boot_prepare_descriptor_bundle(&bundle, &layout)) return 5;
    /* Regioni adiacenti sono valide: l'intervallo stack esclude il suo estremo finale. */
    layout.LinearBase = 0x100000;
    if (!boot_prepare_descriptor_bundle(&bundle, &layout)) return 6;
    make_layout(&layout);
    if (!boot_prepare_descriptor_bundle(&bundle, &layout) ||
        !boot_validate_descriptor_bundle(&bundle, &layout)) return 1;
    if (bundle.Gdtr.Bytes[0] != 39 || bundle.Idtr.Bytes[0] != 255 || bundle.Idtr.Bytes[1] != 1 ||
        bundle.TaskDescriptor.Bytes[5] != 0x89 || BOOT_TSS_SELECTOR != 24) return 2;
    result = check_mutations(&bundle, &layout);
    if (result) return 10 + result;
    for (byte = 0; byte < sizeof(bundle); ++byte) snapshot[byte] = bytes[byte];
    boot_invalidate_descriptor_bundle(&bundle);
    if (bundle.Signature || boot_validate_descriptor_bundle(&bundle, &layout)) return 20;
    for (byte = 8; byte < sizeof(bundle); ++byte)
        if (bytes[byte] != snapshot[byte]) return 21;
    boot_invalidate_descriptor_bundle(0);
    result = check_rejections(&bundle);
    return result ? 30 + result : 0;
}
