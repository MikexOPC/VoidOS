#ifdef KERNEL_PS2_HOST_TEST
#include <assert.h>
#include <stdio.h>
#endif
#include "kernel/drivers/input/ps2/decoder.h"

// ====== decoder_abi_check ======

int kernel_ps2_decoder_check(void)
{
    uint64_t result;
    uint32_t state = 0;
    uint32_t index;
    static const uint32_t pause[] = {0xe1, 0x14, 0x77, 0xe1, 0xf0, 0x14, 0xf0, 0x77};
    if (kernel_ps2_decode(0, 0x1c) != (0x1c | KERNEL_PS2_PRESS)) return 0;
    result = kernel_ps2_decode(0, 0xf0);
    if ((uint32_t)result != KERNEL_PS2_PENDING || (result >> 32) != 1) return 0;
    if (kernel_ps2_decode(1, 0x1c) != (0x1c | KERNEL_PS2_RELEASE)) return 0;
    if (kernel_ps2_decode(2, 0x75) != (0x175 | KERNEL_PS2_PRESS)) return 0;
    if (kernel_ps2_decode(3, 0x75) != (0x175 | KERNEL_PS2_RELEASE)) return 0;
    if (kernel_ps2_decode(3, 0x12) != KERNEL_PS2_PENDING) return 0;
    for (index = 0; index < sizeof(pause) / sizeof(pause[0]); ++index) {
        result = kernel_ps2_decode(state, pause[index]);
        state = (uint32_t)(result >> 32);
        if (index != 7 && (uint32_t)result != KERNEL_PS2_PENDING) return 0;
    }
    if (result != (0x200 | KERNEL_PS2_PULSE)) return 0;
    if (kernel_ps2_decode(0xffffffffU, 0x1c) != KERNEL_PS2_INVALID ||
        kernel_ps2_decode(0, 256) != KERNEL_PS2_INVALID ||
        kernel_ps2_decode(0, 0xffffffffU) != KERNEL_PS2_INVALID) return 0;
    for (index = 0; index < 256; ++index) {
        if (index > 3 && (index < 16 || index > 22) &&
            kernel_ps2_decode(index, 0x1c) != KERNEL_PS2_INVALID) return 0;
    }
    return 1;
}

#ifdef KERNEL_PS2_HOST_TEST

// ====== decoder_host_main ======

int main(void)
{
    assert(kernel_ps2_decoder_check());
    puts("PASS: integer C/Rust ABI, sequences and invalid inputs");
    return 0;
}
#endif
