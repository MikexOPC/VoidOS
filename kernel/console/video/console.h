#ifndef KERNEL_VIDEO_CONSOLE_H
#define KERNEL_VIDEO_CONSOLE_H
#include <stdint.h>
#define KERNEL_CONSOLE_COLUMNS 240U
#define KERNEL_CONSOLE_ROWS 67U
#define KERNEL_CONSOLE_CELLS (KERNEL_CONSOLE_COLUMNS * KERNEL_CONSOLE_ROWS)
#define KERNEL_CONSOLE_DIRTY ((KERNEL_CONSOLE_CELLS + 7U) / 8U)
/* Pixel size of one console cell. The pointer maps pixels to cells with it; the framebuffer
   console font (X86_FB_CELL_WIDTH/HEIGHT) must match, console/runtime.c asserts it. */
#define KERNEL_CONSOLE_CELL_WIDTH 10U
#define KERNEL_CONSOLE_CELL_HEIGHT 20U
#define KERNEL_CONSOLE_INVALID UINT64_MAX
typedef struct {
    uintptr_t  Self;
    uint32_t   Columns, Rows, X, Y, Caret;
    uint32_t   Cells[KERNEL_CONSOLE_CELLS];
    uint8_t    Dirty[KERNEL_CONSOLE_DIRTY];
} KERNEL_VIDEO_CONSOLE;
int kernel_console_init(KERNEL_VIDEO_CONSOLE *console, uint32_t columns, uint32_t rows);
int kernel_console_step(KERNEL_VIDEO_CONSOLE *console, uint32_t scalar);
int kernel_console_peek(const KERNEL_VIDEO_CONSOLE *console, uint32_t start,
    uint32_t *index, uint32_t *scalar);
int kernel_console_ack(KERNEL_VIDEO_CONSOLE *console, uint32_t index, uint32_t scalar);
uint32_t kernel_console_caret_index(uint32_t columns, uint32_t rows, uint32_t x, uint32_t y);
int kernel_console_caret(KERNEL_VIDEO_CONSOLE *console, uint32_t enabled);
int kernel_console_clear(KERNEL_VIDEO_CONSOLE *console);
uint64_t kernel_console_plan(uint32_t columns, uint32_t rows,
    uint32_t x, uint32_t y, uint32_t scalar);
#endif
