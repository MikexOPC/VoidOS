#include "idt.h"
#include "gdt.h"
#include "lib/memory/address_ranges.h"

// ====== output separati dagli indirizzi degli ingressi ====== //

static int valid_arguments(const X86_BOOT_IDT *idt, const X86_IDT_POINTER *pointer, uint64_t base,
                           const uint64_t *targets)
{
    const uint64_t limit = UINT64_C(1) << 47;
    uint32_t vector;
    if (!idt || !pointer || !targets || ((uintptr_t)idt & 15) || ((uintptr_t)targets & 7) ||
        base < 4096 || (base & 15) || base > limit - sizeof(*idt))
        return 0;
    if (os_address_ranges_overlap((uint64_t)(uintptr_t)idt, sizeof(*idt),
                                  (uint64_t)(uintptr_t)pointer, sizeof(*pointer)) ||
        os_address_ranges_overlap((uint64_t)(uintptr_t)idt, sizeof(*idt),
                                  (uint64_t)(uintptr_t)targets, X86_BOOT_EXCEPTION_COUNT * sizeof(*targets)) ||
        os_address_ranges_overlap((uint64_t)(uintptr_t)pointer, sizeof(*pointer),
                                  (uint64_t)(uintptr_t)targets, X86_BOOT_EXCEPTION_COUNT * sizeof(*targets)))
        return 0;
    for (vector = 0; vector < X86_BOOT_EXCEPTION_COUNT; ++vector)
        if (targets[vector] < 4096 || targets[vector] >= limit)
            return 0;
    return 1;
}

// ====== byte di un interrupt gate ring zero senza IST ====== //

static uint8_t gate_byte(uint64_t target, uint32_t byte)
{
    if (byte < 2) return (uint8_t)(target >> (byte * 8));
    if (byte == 2) return X86_BOOT_CODE_SELECTOR;
    if (byte == 5) return 0x8e;
    if (byte == 6 || byte == 7) return (uint8_t)(target >> ((byte - 4) * 8));
    if (byte >= 8 && byte < 12) return (uint8_t)(target >> ((byte - 4) * 8));
    return 0;
}

// ====== costruzione solamente dopo verifica di tutti gli ingressi ====== //

int x86_prepare_boot_idt(X86_BOOT_IDT *idt, X86_IDT_POINTER *pointer, uint64_t linear_base,
                         const uint64_t targets[X86_BOOT_EXCEPTION_COUNT])
{
    uint32_t vector, byte;
    if (!valid_arguments(idt, pointer, linear_base, targets)) return 0;
    for (vector = 0; vector < X86_BOOT_EXCEPTION_COUNT; ++vector)
        for (byte = 0; byte < 16; ++byte)
            idt->Gates[vector][byte] = gate_byte(targets[vector], byte);
    pointer->Bytes[0] = 0xff;
    pointer->Bytes[1] = 1;
    for (byte = 0; byte < 8; ++byte)
        pointer->Bytes[byte + 2] = (uint8_t)(linear_base >> (byte * 8));
    return x86_validate_boot_idt(idt, pointer, linear_base, targets);
}

// ====== controllo del profilo esatto senza dereferenziare gli ingressi ====== //

int x86_validate_boot_idt(const X86_BOOT_IDT *idt, const X86_IDT_POINTER *pointer, uint64_t linear_base,
                          const uint64_t targets[X86_BOOT_EXCEPTION_COUNT])
{
    uint32_t vector, byte;
    if (!valid_arguments(idt, pointer, linear_base, targets)) return 0;
    if (pointer->Bytes[0] != 0xff || pointer->Bytes[1] != 1) return 0;
    for (byte = 0; byte < 8; ++byte)
        if (pointer->Bytes[byte + 2] != (uint8_t)(linear_base >> (byte * 8))) return 0;
    for (vector = 0; vector < X86_BOOT_EXCEPTION_COUNT; ++vector)
        for (byte = 0; byte < 16; ++byte)
            if (idt->Gates[vector][byte] != gate_byte(targets[vector], byte)) return 0;
    return 1;
}
