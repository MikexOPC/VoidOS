#include "arch/x86/lib/descriptors/idt.h"

// ====== decodifica indipendente e controllo dei campi riservati ====== //

static int check_encoding(const X86_BOOT_IDT *idt, const X86_IDT_POINTER *pointer,
                          uint64_t base, const uint64_t *targets)
{
    uint32_t vector, byte;
    uint64_t decoded = 0;
    if (pointer->Bytes[0] != 0xff || pointer->Bytes[1] != 1) return 0;
    for (byte = 0; byte < 8; ++byte)
        decoded |= (uint64_t)pointer->Bytes[byte + 2] << (byte * 8);
    if (decoded != base) return 0;
    for (vector = 0; vector < 32; ++vector) {
        const uint8_t *gate = idt->Gates[vector];
        decoded = (uint64_t)gate[0] | ((uint64_t)gate[1] << 8) |
                  ((uint64_t)gate[6] << 16) | ((uint64_t)gate[7] << 24);
        for (byte = 0; byte < 4; ++byte)
            decoded |= (uint64_t)gate[byte + 8] << (32 + byte * 8);
        if (decoded != targets[vector] || gate[2] != 8 || gate[3] || gate[4] || gate[5] != 0x8e)
            return 0;
        for (byte = 12; byte < 16; ++byte)
            if (gate[byte]) return 0;
    }
    return 1;
}

// ====== invalidazione di ciascun bit di IDT e IDTR ====== //

static int check_mutations(X86_BOOT_IDT *idt, X86_IDT_POINTER *pointer, const uint64_t *targets)
{
    uint32_t vector, byte, bit;
    if (!x86_prepare_boot_idt(idt, pointer, 4096, targets)) return 1;
    for (vector = 0; vector < 32; ++vector) {
        for (byte = 0; byte < 16; ++byte) {
            for (bit = 0; bit < 8; ++bit) {
                idt->Gates[vector][byte] ^= (uint8_t)(1U << bit);
                if (x86_validate_boot_idt(idt, pointer, 4096, targets)) return 2;
                idt->Gates[vector][byte] ^= (uint8_t)(1U << bit);
            }
        }
    }
    for (byte = 0; byte < 10; ++byte) {
        for (bit = 0; bit < 8; ++bit) {
            pointer->Bytes[byte] ^= (uint8_t)(1U << bit);
            if (x86_validate_boot_idt(idt, pointer, 4096, targets)) return 3;
            pointer->Bytes[byte] ^= (uint8_t)(1U << bit);
        }
    }
    return !x86_validate_boot_idt(idt, pointer, 4096, targets) ? 4 : 0;
}

// ====== sentinelle conservate su errori anche nell'ultimo ingresso ====== //

static int check_rejections(void)
{
    const uint64_t invalid_base[] = {0, 16, 4097, (UINT64_C(1) << 47) - 496,
                                     UINT64_C(1) << 47, UINT64_MAX};
    // K1: the upper half is valid for targets; the non-canonical hole (both ends) is not.
    const uint64_t invalid_target[] = {0, 8, UINT64_C(1) << 47, UINT64_C(0xFFFF7FFFFFFFFFFF)};
    X86_BOOT_IDT idt;
    X86_IDT_POINTER pointer;
    uint64_t targets[32];
    uint32_t index, byte, vector;
    for (vector = 0; vector < 32; ++vector) targets[vector] = 4096 + vector;
    for (vector = 0; vector < 32; ++vector)
        for (byte = 0; byte < 16; ++byte) idt.Gates[vector][byte] = 0xa5;
    for (byte = 0; byte < 10; ++byte) pointer.Bytes[byte] = 0x5a;
    for (index = 0; index < sizeof(invalid_base) / sizeof(invalid_base[0]); ++index)
        if (x86_prepare_boot_idt(&idt, &pointer, invalid_base[index], targets)) return 1;
    for (vector = 0; vector < 32; ++vector) {
        uint64_t original = targets[vector];
        for (index = 0; index < sizeof(invalid_target) / sizeof(invalid_target[0]); ++index) {
            targets[vector] = invalid_target[index];
            if (x86_prepare_boot_idt(&idt, &pointer, 4096, targets)) return 2;
        }
        targets[vector] = original;
    }
    if (x86_prepare_boot_idt(0, &pointer, 4096, targets) ||
        x86_prepare_boot_idt(&idt, 0, 4096, targets) ||
        x86_prepare_boot_idt(&idt, &pointer, 4096, 0)) return 3;
    for (vector = 0; vector < 32; ++vector)
        for (byte = 0; byte < 16; ++byte)
            if (idt.Gates[vector][byte] != 0xa5) return 4;
    for (byte = 0; byte < 10; ++byte)
        if (pointer.Bytes[byte] != 0x5a) return 5;
    if (x86_prepare_boot_idt(&idt, (X86_IDT_POINTER *)idt.Gates[31], 4096, targets) ||
        x86_prepare_boot_idt(&idt, &pointer, 4096, (const uint64_t *)&idt) ||
        x86_prepare_boot_idt((X86_BOOT_IDT *)targets, &pointer, 4096, targets)) return 6;
    return 0;
}

// ====== alias parziali in entrambe le direzioni senza modifiche alla memoria ====== //

static int check_aliases(void)
{
    __declspec(align(16)) uint8_t storage[1024];
    uint64_t targets[32];
    X86_BOOT_IDT idt;
    X86_IDT_POINTER pointer;
    uint32_t offset, byte;
    for (offset = 0; offset < 32; ++offset) targets[offset] = 4096 + offset;
    for (offset = 0; offset < 512; ++offset) {
        for (byte = 0; byte < sizeof(storage); ++byte) storage[byte] = 0xa5;
        if (x86_prepare_boot_idt((X86_BOOT_IDT *)storage, (X86_IDT_POINTER *)(storage + offset),
                                 4096, targets)) return 1;
        for (byte = 0; byte < sizeof(storage); ++byte)
            if (storage[byte] != 0xa5) return 2;
    }
    for (offset = 0; offset < 512; offset += 8) {
        for (byte = 0; byte < sizeof(storage); ++byte) storage[byte] = 0xa5;
        if (x86_prepare_boot_idt((X86_BOOT_IDT *)storage, &pointer, 4096,
                                 (const uint64_t *)(storage + offset))) return 3;
        for (byte = 0; byte < sizeof(storage); ++byte)
            if (storage[byte] != 0xa5) return 4;
    }
    /* L'array input comincia prima degli output ma li include alla fine. */
    if (x86_prepare_boot_idt((X86_BOOT_IDT *)(storage + 240), &pointer, 4096,
                             (const uint64_t *)storage) ||
        x86_prepare_boot_idt(&idt, (X86_IDT_POINTER *)(storage + 248), 4096,
                             (const uint64_t *)storage) ||
        x86_prepare_boot_idt((X86_BOOT_IDT *)storage, (X86_IDT_POINTER *)(storage + 504),
                             4096, targets) ||
        x86_prepare_boot_idt((X86_BOOT_IDT *)(storage + 1), &pointer, 4096, targets) ||
        x86_prepare_boot_idt(&idt, &pointer, 4096, (const uint64_t *)(storage + 1))) return 5;
    for (byte = 0; byte < sizeof(storage); ++byte)
        if (storage[byte] != 0xa5) return 6;
    return 0;
}

// ====== indirizzi riproducibili e guardie degli oggetti ====== //

__declspec(dllexport) int run_idt_tests(void)
{
    struct {
        uint64_t Before;
        X86_BOOT_IDT Idt;
        uint64_t Between;
        X86_IDT_POINTER Pointer;
        uint64_t After;
    } guarded;
    uint64_t targets[32], state = UINT64_C(0x75938ea17624bcdf);
    uint32_t iteration, vector;
    int result;
    guarded.Before = guarded.Between = guarded.After = UINT64_C(0xabcdef1973650248);
    for (iteration = 0; iteration < 514; ++iteration) {
        uint64_t base;
        state = state * UINT64_C(6364136223846793005) + 1;
        base = (state & ((UINT64_C(1) << 47) - 1)) & ~UINT64_C(15);
        if (base < 4096 || base > (UINT64_C(1) << 47) - 512) base = 4096;
        if (iteration == 512) base = 4096;
        if (iteration == 513) base = (UINT64_C(1) << 47) - 512;
        for (vector = 0; vector < 32; ++vector) {
            state = state * UINT64_C(6364136223846793005) + 1;
            targets[vector] = 4096 + (state % ((UINT64_C(1) << 47) - 4096));
        }
        if (!x86_prepare_boot_idt(&guarded.Idt, &guarded.Pointer, base, targets) ||
            !check_encoding(&guarded.Idt, &guarded.Pointer, base, targets)) return 10;
        if (guarded.Before != UINT64_C(0xabcdef1973650248) ||
            guarded.Between != guarded.Before || guarded.After != guarded.Before) return 11;
    }
    result = check_mutations(&guarded.Idt, &guarded.Pointer, targets);
    if (result) return 20 + result;
    result = check_rejections();
    if (result) return 30 + result;
    result = check_aliases();
    return result ? 40 + result : 0;
}
