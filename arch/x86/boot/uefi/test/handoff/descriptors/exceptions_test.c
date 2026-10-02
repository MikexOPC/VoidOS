#include "handoff/descriptors/exceptions.h"

// ====== controllo del codice macchina senza eseguire istruzioni privilegiate ====== //

__declspec(dllexport) int run_exception_entry_tests(void)
{
    const uint8_t *stop = (const uint8_t *)(uintptr_t)x86_boot_exception_stop;
    BOOT_IDT_DRAFT draft;
    uint32_t vector, byte;
    if (stop[0] != 0xfa || stop[1] != 0xf4 || stop[2] != 0xeb || stop[3] != 0xfd)
        return 1;
    for (vector = 0; vector < 32; ++vector) {
        const uint8_t *entry = (const uint8_t *)(uintptr_t)x86_boot_exception_targets[vector];
        uint32_t immediate = 0, displacement = 0;
        uint64_t destination;
        if (entry[0] != 0xb8 || entry[5] != 0xe9) return 2;
        for (byte = 0; byte < 4; ++byte) {
            immediate |= (uint32_t)entry[byte + 1] << (byte * 8);
            displacement |= (uint32_t)entry[byte + 6] << (byte * 8);
        }
        if (immediate != vector) return 3;
        /* Tutti gli ingressi precedono lo stop; salti in avanti senza conversioni signed. */
        if (displacement > 320) return 4;
        destination = x86_boot_exception_targets[vector] + 10 + displacement;
        if (destination != (uint64_t)(uintptr_t)stop) return 5;
        if (vector && x86_boot_exception_targets[vector] != x86_boot_exception_targets[vector - 1] + 10)
            return 6;
    }
    if (!x86_prepare_boot_idt(&draft.Table, &draft.Pointer, (uint64_t)(uintptr_t)&draft.Table,
                              x86_boot_exception_targets) ||
        !x86_validate_boot_idt(&draft.Table, &draft.Pointer, (uint64_t)(uintptr_t)&draft.Table,
                               x86_boot_exception_targets)) return 7;
    if (boot_prepare_idt_draft(0, 0) != EFI_INVALID_PARAMETER) return 8;
    return 0;
}
