#ifndef KERNEL_CONSOLE_RENDER_H
#define KERNEL_CONSOLE_RENDER_H
#include "console.h"
#define KERNEL_CONSOLE_RENDER_IDLE 1U
#define KERNEL_CONSOLE_RENDER_MORE 2U
#define KERNEL_CONSOLE_RENDER_BUDGET 16U
typedef int (*KERNEL_CONSOLE_DRAW)(void *context, uint32_t column, uint32_t row,
    uint32_t scalar, uint64_t *ticks);
typedef struct {
    uintptr_t  Self;
    KERNEL_VIDEO_CONSOLE  *Console;
    KERNEL_CONSOLE_DRAW  Draw;
    void  *Context;
    uint64_t  CellsDone, MaxTicks;
    uint32_t  Next, Busy;
} KERNEL_CONSOLE_RENDER;
int kernel_console_render_init(KERNEL_CONSOLE_RENDER *render, KERNEL_VIDEO_CONSOLE *console,
    KERNEL_CONSOLE_DRAW draw, void *context);
uint32_t kernel_console_render_flush(KERNEL_CONSOLE_RENDER *render, uint32_t budget);
#endif
