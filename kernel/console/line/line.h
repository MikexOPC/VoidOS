#ifndef KERNEL_PS2_LINE_H
#define KERNEL_PS2_LINE_H
#include <stdint.h>
#define KERNEL_PS2_LINE_CAPACITY 256U
#define KERNEL_PS2_LINE_INVALID UINT32_MAX
#define KERNEL_PS2_LINE_APPEND 1U
#define KERNEL_PS2_LINE_ERASE 2U
#define KERNEL_PS2_LINE_SUBMIT 3U
#define KERNEL_PS2_LINE_NONE 4U
#define KERNEL_PS2_LINE_FULL 5U
typedef struct {
    uint32_t  Length;
    char      Data[KERNEL_PS2_LINE_CAPACITY + 1];
} KERNEL_PS2_LINE;
/* Trusted live kernel object, single task owner. Initialize with clear;
   modify only through step/clear. SUBMIT retains the NUL-terminated buffer:
   use synchronously, then clear only after successful delivery. No IRQ use. */
void kernel_ps2_line_clear(KERNEL_PS2_LINE *line);
uint32_t kernel_ps2_line_step(KERNEL_PS2_LINE *line, uint32_t input);
/* Integer-only Rust boundary, high32=next length, low32=status.
   Tail contains last min(length,4) bytes packed in little-endian order. */
uint64_t kernel_ps2_line_plan(uint32_t length, uint32_t tail, uint32_t input);
#endif
