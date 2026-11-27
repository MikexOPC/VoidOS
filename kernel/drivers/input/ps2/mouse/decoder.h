#ifndef KERNEL_PS2_MOUSE_DECODER_H
#define KERNEL_PS2_MOUSE_DECODER_H

#include <stdint.h>

#define KERNEL_PS2_MOUSE_PENDING  (1U << 24)
#define KERNEL_PS2_MOUSE_INVALID  (1U << 25)
#define KERNEL_PS2_MOUSE_OVERFLOW (1U << 26)

typedef struct {
    uint32_t  State;
    uint32_t  HasWheel;
    uint32_t  PendingWheel;
    uint32_t  PendingOutput;
} KERNEL_PS2_MOUSE_DECODER;

typedef struct {
    int32_t   DeltaX;
    int32_t   DeltaY;
    uint32_t  Buttons;
    int32_t   Wheel;
} KERNEL_PS2_MOUSE_EVENT;

typedef enum {
    KERNEL_PS2_MOUSE_WAIT,
    KERNEL_PS2_MOUSE_EVENT_READY,
    KERNEL_PS2_MOUSE_REJECTED,
    KERNEL_PS2_MOUSE_SATURATED
} KERNEL_PS2_MOUSE_RESULT;

uint64_t kernel_ps2_mouse_decode(uint32_t state, uint32_t byte);
void kernel_ps2_mouse_reset(KERNEL_PS2_MOUSE_DECODER *decoder);
void kernel_ps2_mouse_set_wheel(KERNEL_PS2_MOUSE_DECODER *decoder, int enabled);
KERNEL_PS2_MOUSE_RESULT kernel_ps2_mouse_feed(
    KERNEL_PS2_MOUSE_DECODER *decoder, uint32_t byte,
    KERNEL_PS2_MOUSE_EVENT *event);

#endif
