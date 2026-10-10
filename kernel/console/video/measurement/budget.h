#ifndef KERNEL_CONSOLE_MEASUREMENT_H
#define KERNEL_CONSOLE_MEASUREMENT_H
#include "../render.h"
typedef uint32_t (*KERNEL_CONSOLE_CLOCK)(void *context);
typedef struct {
    uintptr_t  Self;
    KERNEL_CONSOLE_RENDER  *Render;
    KERNEL_CONSOLE_CLOCK  Read;
    void  *Context;
    uint64_t  Cells, SlowCells;
    uint32_t  Mask, LastTicks, MaxTicks, Busy, Failed;
} KERNEL_CONSOLE_MEASUREMENT;
uint64_t kernel_console_pm_sample(uint32_t start, uint32_t end, uint32_t mask);
uint32_t kernel_console_pm_limit(void);
uint32_t kernel_console_pm_goal(void);
int kernel_console_measure_init(KERNEL_CONSOLE_MEASUREMENT *owner, KERNEL_CONSOLE_RENDER *render,
    KERNEL_CONSOLE_CLOCK read, void *context, uint32_t mask);
uint32_t kernel_console_measure_step(KERNEL_CONSOLE_MEASUREMENT *owner);
#endif
