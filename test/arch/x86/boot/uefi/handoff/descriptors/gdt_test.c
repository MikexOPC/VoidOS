#include "arch/x86/lib/descriptors/gdt.h"
#include "handoff/descriptors/prepare.h"

// ====== oracolo dei byte attesi e decodifica separata dal costruttore ====== //

static int check_encoding(const X86_BOOT_GDT *gdt, const X86_GDT_POINTER *pointer, uint64_t base)
{
    const uint8_t expected[24] = {
        0, 0, 0, 0, 0, 0, 0, 0,
        0xff, 0xff, 0, 0, 0, 0x9b, 0xaf, 0,
        0xff, 0xff, 0, 0, 0, 0x93, 0xcf, 0
    };
    const uint8_t *bytes = (const uint8_t *)gdt;
    uint64_t decoded = 0;
    uint32_t index;
    for (index = 0; index < 24; ++index)
        if (bytes[index] != expected[index]) return 0;
    for (index = 0; index < 8; ++index)
        decoded |= (uint64_t)pointer->Bytes[2 + index] << (8 * index);
    if (decoded != base || pointer->Bytes[0] != 23 || pointer->Bytes[1]) return 0;
    /* L=1 e D=0 nel codice, L=0 nei dati; DPL=0 e accessed=1. */
    return ((gdt->Entries[1] >> 53) & 3) == 1 &&
           !(gdt->Entries[2] & (UINT64_C(1) << 53)) &&
           !(gdt->Entries[1] & (UINT64_C(3) << 45)) &&
           !(gdt->Entries[2] & (UINT64_C(3) << 45)) &&
           (gdt->Entries[1] & (UINT64_C(1) << 40)) &&
           (gdt->Entries[2] & (UINT64_C(1) << 40)) &&
           X86_BOOT_CODE_SELECTOR == 8 && X86_BOOT_DATA_SELECTOR == 16;
}

// ====== invalidazione di ogni bit del formato GDT e GDTR ====== //

static int check_mutations(void)
{
    X86_BOOT_GDT gdt;
    X86_GDT_POINTER pointer;
    uint32_t entry, bit;
    if (!x86_prepare_boot_gdt(&gdt, &pointer, 0x12345000)) return 1;
    for (entry = 0; entry < 3; ++entry) {
        for (bit = 0; bit < 64; ++bit) {
            gdt.Entries[entry] ^= UINT64_C(1) << bit;
            if (x86_validate_boot_gdt(&gdt, &pointer, 0x12345000)) return 2;
            gdt.Entries[entry] ^= UINT64_C(1) << bit;
        }
    }
    for (entry = 0; entry < 10; ++entry) {
        for (bit = 0; bit < 8; ++bit) {
            pointer.Bytes[entry] ^= (uint8_t)(1U << bit);
            if (x86_validate_boot_gdt(&gdt, &pointer, 0x12345000)) return 3;
            pointer.Bytes[entry] ^= (uint8_t)(1U << bit);
        }
    }
    return !x86_validate_boot_gdt(&gdt, &pointer, 0x12345000) ? 4 : 0;
}

// ====== nessuna modifica degli output su indirizzi o alias invalidi ====== //

static int check_rejections(void)
{
    const uint64_t invalid[] = {
        0, 1, 4097, (UINT64_C(1) << 47) - 16,
        UINT64_C(1) << 47, UINT64_C(1) << 48, UINT64_MAX,
        UINT64_C(0xffff800000001000)
    };
    X86_BOOT_GDT gdt;
    X86_GDT_POINTER pointer;
    uint32_t index, byte;
    union {
        uint64_t Alignment;
        uint8_t Bytes[64];
    } storage;
    for (index = 0; index < sizeof(invalid) / sizeof(invalid[0]); ++index) {
        for (byte = 0; byte < 3; ++byte) gdt.Entries[byte] = UINT64_MAX;
        for (byte = 0; byte < 10; ++byte) pointer.Bytes[byte] = 0xa5;
        if (x86_prepare_boot_gdt(&gdt, &pointer, invalid[index]) ||
            x86_validate_boot_gdt(&gdt, &pointer, invalid[index])) return 1;
        for (byte = 0; byte < 3; ++byte)
            if (gdt.Entries[byte] != UINT64_MAX) return 2;
        for (byte = 0; byte < 10; ++byte)
            if (pointer.Bytes[byte] != 0xa5) return 3;
    }
    if (x86_prepare_boot_gdt(0, &pointer, 4096) ||
        x86_prepare_boot_gdt(&gdt, 0, 4096) ||
        x86_validate_boot_gdt(0, &pointer, 4096) ||
        x86_validate_boot_gdt(&gdt, 0, 4096)) return 4;
    for (index = 0; index < 24; ++index) {
        X86_BOOT_GDT *aliased = (X86_BOOT_GDT *)storage.Bytes;
        X86_GDT_POINTER *operand = (X86_GDT_POINTER *)&storage.Bytes[index];
        for (byte = 0; byte < sizeof(storage); ++byte) storage.Bytes[byte] = 0x5a;
        if (x86_prepare_boot_gdt(aliased, operand, 4096) ||
            x86_validate_boot_gdt(aliased, operand, 4096)) return 5;
        for (byte = 0; byte < sizeof(storage); ++byte)
            if (storage.Bytes[byte] != 0x5a) return 6;
    }
    return 0;
}

// ====== guardie degli oggetti e basi riproducibili nel profilo supportato ====== //

__declspec(dllexport) int run_gdt_tests(void)
{
    struct {
        uint64_t Before;
        X86_BOOT_GDT Gdt;
        uint64_t Between;
        X86_GDT_POINTER Pointer;
        uint64_t After;
    } guarded;
    uint64_t state = UINT64_C(0x981cbd42f06a3571);
    uint32_t index;
    int result;
    guarded.Before = guarded.Between = guarded.After = UINT64_C(0x3141592653589793);
    for (index = 0; index < 514; ++index) {
        uint64_t base;
        state = state * UINT64_C(6364136223846793005) + 1;
        base = (state & ((UINT64_C(1) << 47) - 1)) & ~UINT64_C(7);
        if (!base || base > (UINT64_C(1) << 47) - 24) base = 4096;
        if (index == 512) base = 8;
        if (index == 513) base = (UINT64_C(1) << 47) - 24;
        if (!x86_prepare_boot_gdt(&guarded.Gdt, &guarded.Pointer, base) ||
            !check_encoding(&guarded.Gdt, &guarded.Pointer, base) ||
            !x86_validate_boot_gdt(&guarded.Gdt, &guarded.Pointer, base)) return 10;
        if (guarded.Before != UINT64_C(0x3141592653589793) ||
            guarded.Between != guarded.Before || guarded.After != guarded.Before) return 11;
    }
    result = check_mutations();
    if (result) return 20 + result;
    result = check_rejections();
    if (result) return 30 + result;
    if (boot_prepare_gdt_draft(0, 0) != EFI_INVALID_PARAMETER) return 40;
    return 0;
}
