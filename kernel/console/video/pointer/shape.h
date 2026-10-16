#ifndef KERNEL_CONSOLE_POINTER_SHAPE_H
#define KERNEL_CONSOLE_POINTER_SHAPE_H
#include <stdint.h>
#define KERNEL_POINTER_SIZE 18U
#define KERNEL_POINTER_HOTSPOT_X 0U
#define KERNEL_POINTER_HOTSPOT_Y 0U
typedef struct {
    uint32_t  X, Y, Visible;
} KERNEL_POINTER_IMAGE;
uint32_t kernel_pointer_sample(uint32_t x, uint32_t y);
uint32_t kernel_pointer_blend(uint32_t sample, uint32_t background);
uint32_t kernel_pointer_blend(uint32_t sample, uint32_t background);
#endif
