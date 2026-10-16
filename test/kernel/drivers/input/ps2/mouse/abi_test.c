#include "kernel/drivers/input/ps2/mouse/decoder.h"
#include <assert.h>
#include <stdio.h>

_Static_assert(sizeof(KERNEL_PS2_MOUSE_DECODER) == 4, "decoder storage");
_Static_assert(sizeof(KERNEL_PS2_MOUSE_EVENT) == 12, "event storage");

// ====== exhaustive_packets ======

static void exhaustive_packets(void)
{
    KERNEL_PS2_MOUSE_DECODER  decoder = {0};
    KERNEL_PS2_MOUSE_EVENT    event;
    uint32_t                 signs;
    uint32_t                 buttons;
    uint32_t                 x;
    uint32_t                 y;

    for (signs = 0; signs < 4; ++signs) {
        for (buttons = 0; buttons < 8; ++buttons) {
            for (x = 0; x < 256; ++x) {
                for (y = 0; y < 256; ++y) {
                    assert(kernel_ps2_mouse_feed(&decoder,
                        8U | (signs << 4) | buttons, &event) == KERNEL_PS2_MOUSE_WAIT);
                    assert(event.DeltaX == 0 && event.DeltaY == 0 && event.Buttons == 0);
                    assert(kernel_ps2_mouse_feed(&decoder, x, &event) == KERNEL_PS2_MOUSE_WAIT);
                    assert(kernel_ps2_mouse_feed(&decoder, y, &event) == KERNEL_PS2_MOUSE_EVENT_READY);
                    assert(event.DeltaX == (int32_t)x - ((signs & 1U) ? 256 : 0));
                    assert(event.DeltaY == (int32_t)y - ((signs & 2U) ? 256 : 0));
                    assert(event.Buttons == buttons && decoder.State == 0);
                }
            }
        }
    }
}

// ====== errors_and_reset ======

static void errors_and_reset(void)
{
    KERNEL_PS2_MOUSE_DECODER  decoder = {0};
    KERNEL_PS2_MOUSE_EVENT    event = {1, 2, 7};
    uint32_t                 header;

    assert(kernel_ps2_mouse_feed(0, 8, &event) == KERNEL_PS2_MOUSE_REJECTED);
    assert(event.DeltaX == 1 && event.DeltaY == 2 && event.Buttons == 7);
    assert(kernel_ps2_mouse_feed(&decoder, 8, 0) == KERNEL_PS2_MOUSE_REJECTED);
    assert(decoder.State == 0);
    assert(kernel_ps2_mouse_feed(&decoder, 256, &event) == KERNEL_PS2_MOUSE_REJECTED);
    assert(event.DeltaX == 0 && event.DeltaY == 0 && event.Buttons == 0);
    for (header = 0; header < 256; ++header) {
        if ((header & 8U) == 0) {
            assert(kernel_ps2_mouse_feed(&decoder, header, &event) == KERNEL_PS2_MOUSE_REJECTED);
        } else if ((header & 0xc0U) != 0) {
            assert(kernel_ps2_mouse_feed(&decoder, header, &event) == KERNEL_PS2_MOUSE_WAIT);
            assert(kernel_ps2_mouse_feed(&decoder, 255, &event) == KERNEL_PS2_MOUSE_WAIT);
            assert(kernel_ps2_mouse_feed(&decoder, 255, &event) == KERNEL_PS2_MOUSE_SATURATED);
            assert(event.DeltaX == 0 && event.DeltaY == 0 && event.Buttons == 0);
            assert(decoder.State == 0);
        }
    }
    decoder.State = UINT32_MAX;
    assert(kernel_ps2_mouse_feed(&decoder, 8, &event) == KERNEL_PS2_MOUSE_REJECTED);
    assert(decoder.State == 0);
    assert(kernel_ps2_mouse_feed(&decoder, 8, &event) == KERNEL_PS2_MOUSE_WAIT);
    kernel_ps2_mouse_reset(&decoder);
    assert(decoder.State == 0);
    kernel_ps2_mouse_reset(0);
}

// ====== overlapping_outputs ======

static void overlapping_outputs(void)
{
    union {
        KERNEL_PS2_MOUSE_EVENT  Event;
        uint32_t               Words[3];
    } storage;
    uint32_t  offset;

    for (offset = 0; offset < 3; ++offset) {
        storage.Words[0] = 0x801;
        storage.Words[1] = 0x802;
        storage.Words[2] = 0x803;
        assert(kernel_ps2_mouse_feed((KERNEL_PS2_MOUSE_DECODER *)&storage.Words[offset],
            8, &storage.Event) == KERNEL_PS2_MOUSE_REJECTED);
        assert(storage.Words[0] == 0x801 && storage.Words[1] == 0x802 && storage.Words[2] == 0x803);
    }
}

// ====== mouse_decoder_host_main ======

int main(void)
{
    exhaustive_packets();
    errors_and_reset();
    overlapping_outputs();
    puts("PASS: 2097152 packets, rejection/reset/overflow/overlap; no allocations");
    return 0;
}
