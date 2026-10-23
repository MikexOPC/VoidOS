#ifndef KERNEL_CONSOLE_POINTER_STATE_H
#define KERNEL_CONSOLE_POINTER_STATE_H
#include "../console.h"
#include "shape.h"
typedef struct {
    uintptr_t               Self;
    KERNEL_VIDEO_CONSOLE     *View;
    KERNEL_POINTER_IMAGE    Image;
    uint32_t                Buttons, Closed;
    uint64_t                Moves;
} KERNEL_CONSOLE_POINTER;
uint64_t kernel_pointer_plan(uint32_t width, uint32_t height, uint32_t x, uint32_t y,
    int32_t dx, int32_t dy);
int kernel_pointer_init(KERNEL_CONSOLE_POINTER *owner, KERNEL_VIDEO_CONSOLE *view);
int kernel_pointer_valid(const KERNEL_CONSOLE_POINTER *owner);
int kernel_pointer_move(KERNEL_CONSOLE_POINTER *owner, int32_t dx, int32_t dy, uint32_t buttons);
int kernel_pointer_request(KERNEL_CONSOLE_POINTER *owner, int32_t dx, int32_t dy, uint32_t buttons);
int kernel_pointer_clamp(KERNEL_CONSOLE_POINTER *owner);
int kernel_pointer_hide(KERNEL_CONSOLE_POINTER *owner);
int kernel_pointer_close(KERNEL_CONSOLE_POINTER *owner);
#endif
