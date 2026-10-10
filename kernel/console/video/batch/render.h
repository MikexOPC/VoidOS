#ifndef KERNEL_CONSOLE_BATCH_RENDER_H
#define KERNEL_CONSOLE_BATCH_RENDER_H
#include "../render.h"
#define KERNEL_CONSOLE_BATCH_LIMIT 8U
typedef int (*KERNEL_CONSOLE_GROUP_DRAW)(void *context, uint32_t column, uint32_t row,
    const uint32_t *scalars, uint32_t count, uint64_t *ticks);
typedef struct {
    uintptr_t                  Self;
    KERNEL_CONSOLE_RENDER       *Render;
    KERNEL_CONSOLE_GROUP_DRAW   Draw;
    void                       *Context;
    uint64_t                   Groups;
    uint32_t                   Busy, Failed;
} KERNEL_CONSOLE_BATCH;
int kernel_console_batch_init(KERNEL_CONSOLE_BATCH *owner, KERNEL_CONSOLE_RENDER *render,
    KERNEL_CONSOLE_GROUP_DRAW draw, void *context);
uint32_t kernel_console_batch_flush(KERNEL_CONSOLE_BATCH *owner, uint32_t budget);
#endif
