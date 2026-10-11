#include "handoff/transition/capsule.h"

static __declspec(align(4096)) UINT8 page[4096];

// ====== copia verificata e mutazione di ogni bit incluso padding ====== //

__declspec(dllexport) int run_capsule_tests(void)
{
    UINT32 byte, bit;
    if (!boot_prepare_capsule(page, 4096) || !boot_validate_capsule(page, 4096)) return 1;
    if (boot_prepare_capsule(page + 1, 4095) || boot_validate_capsule(page, 4095) ||
        boot_prepare_capsule(0, 4096) || boot_validate_capsule(0, 4096)) return 2;
    for (byte = 0; byte < 4096; ++byte) {
        UINT8 original = page[byte];
        for (bit = 0; bit < 8; ++bit) {
            page[byte] = original ^ (UINT8)(1U << bit);
            if (boot_validate_capsule(page, 4096)) return 3;
        }
        page[byte] = original;
    }
    if (!boot_validate_capsule(page, 4096)) return 4;
    if (!boot_capsule_check_addresses(0x100000, 0x200000, 0x400000, 0x500000, 65536) ||
        boot_capsule_check_addresses(0x100000, 0x100040, 0x400000, 0x500000, 65536)) return 5;
    return 0;
}
