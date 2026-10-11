#include <assert.h>
#include <stdio.h>
#include "kernel/input/keyboard/text.h"
#include "kernel/drivers/input/ps2/decoder.h"
static unsigned checks;
#define CHECK(e) do { ++checks; assert(e); } while (0)

// ====== decoder_to_text_integer_abi ======

int main(void)
{
    uint32_t decoder = 0, state = 0;
    const uint8_t bytes[] = {0xe0,0x11,0x24,0xf0,0x24,0xe0,0xf0,0x11,0x12,0x1c,0xf0,0x12};
    const uint32_t expected[] = {0,0x20ac,0,0,0,0x41,0};
    uint32_t index = 0;
    for (unsigned byte = 0; byte < sizeof(bytes); ++byte) {
        uint64_t decoded = kernel_ps2_decode(decoder,bytes[byte]);
        decoder = (uint32_t)(decoded >> 32);
        if ((uint32_t)decoded == KERNEL_PS2_PENDING) continue;
        CHECK((uint32_t)decoded != KERNEL_PS2_INVALID && index < sizeof(expected)/sizeof(expected[0]));
        uint64_t result = kernel_ps2_text(state,(uint32_t)decoded);
        state = (uint32_t)(result >> 32);
        CHECK((uint32_t)result == expected[index]);
        ++index;
    }
    CHECK(!decoder && !state && index == sizeof(expected)/sizeof(expected[0]));
    CHECK(kernel_ps2_text(256,KERNEL_PS2_PRESS | 0x1c) == KERNEL_PS2_TEXT_INVALID);
    for (uint32_t scalar = 0; scalar <= 0x110000; ++scalar) {
        uint64_t encoded = kernel_ps2_utf8(scalar);
        if ((scalar >= 0xd800 && scalar <= 0xdfff) || scalar > 0x10ffff) CHECK(!encoded);
        else {
            uint32_t length = (uint32_t)(encoded >> 32);
            CHECK(length == (scalar < 0x80 ? 1U : scalar < 0x800 ? 2U : scalar < 0x10000 ? 3U : 4U));
        }
    }
    CHECK(kernel_ps2_utf8(0xe8) == ((2ULL << 32) | 0xa8c3));
    CHECK(kernel_ps2_utf8(0x20ac) == ((3ULL << 32) | 0xac82e2));
    CHECK(!kernel_ps2_utf8(UINT32_MAX));
    printf("PASS: Italian text C/Rust %u checks, decoder pipeline and scalar bounds\n",checks);
    return 0;
}
