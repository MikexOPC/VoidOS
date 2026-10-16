#include "decoder.h"

// ====== kernel_ps2_mouse_reset ======

void kernel_ps2_mouse_reset(KERNEL_PS2_MOUSE_DECODER *decoder)
{
    if (decoder != 0) {
        decoder->State = 0;
    }
}

// ====== kernel_ps2_mouse_signed_delta ======

static int32_t kernel_ps2_mouse_signed_delta(uint32_t value)
{
    return (int32_t)(value & 255U) - (int32_t)(value & 256U);
}

// ====== kernel_ps2_mouse_feed ======

KERNEL_PS2_MOUSE_RESULT kernel_ps2_mouse_feed(
    KERNEL_PS2_MOUSE_DECODER *decoder, uint32_t byte,
    KERNEL_PS2_MOUSE_EVENT *event)
{
    uint64_t  transition;
    uint32_t  output;
    uintptr_t decoder_address = (uintptr_t)decoder;
    uintptr_t event_address = (uintptr_t)event;

    if (decoder == 0 || event == 0 ||
        (decoder_address <= event_address ?
            event_address - decoder_address < sizeof(*decoder) :
            decoder_address - event_address < sizeof(*event))) {
        return KERNEL_PS2_MOUSE_REJECTED;
    }
    event->DeltaX = 0;
    event->DeltaY = 0;
    event->Buttons = 0;
    transition = kernel_ps2_mouse_decode(decoder->State, byte);
    decoder->State = (uint32_t)(transition >> 32);
    output = (uint32_t)transition;
    if ((output & KERNEL_PS2_MOUSE_INVALID) != 0) {
        return KERNEL_PS2_MOUSE_REJECTED;
    }
    if ((output & KERNEL_PS2_MOUSE_PENDING) != 0) {
        return KERNEL_PS2_MOUSE_WAIT;
    }
    if ((output & KERNEL_PS2_MOUSE_OVERFLOW) != 0) {
        return KERNEL_PS2_MOUSE_SATURATED;
    }
    event->DeltaX = kernel_ps2_mouse_signed_delta(output);
    event->DeltaY = kernel_ps2_mouse_signed_delta(output >> 9);
    event->Buttons = (output >> 18) & 7U;
    return KERNEL_PS2_MOUSE_EVENT_READY;
}
