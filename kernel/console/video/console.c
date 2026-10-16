#include "console.h"

// ====== valid ======

static int valid(const KERNEL_VIDEO_CONSOLE *console)
{
    return console && console->Self == (uintptr_t)console &&
        (console->Caret == UINT32_MAX || console->Caret < console->Columns * console->Rows) &&
        kernel_console_plan(console->Columns, console->Rows, console->X, console->Y, 13) !=
            KERNEL_CONSOLE_INVALID;
}

// ====== mark ======

static void mark(KERNEL_VIDEO_CONSOLE *console, uint32_t index)
{
    console->Dirty[index / 8] |= (uint8_t)(1U << (index % 8));
}

// ====== kernel_console_clear ======

int kernel_console_clear(KERNEL_VIDEO_CONSOLE *console)
{
    uint32_t index, count;
    if (!valid(console)) return 0;
    count = console->Columns * console->Rows;
    for (index = 0; index < count; ++index) {
        console->Cells[index] = 32;
        mark(console, index);
    }
    console->X = console->Y = 0;
    if (console->Caret != UINT32_MAX) console->Caret = 0;
    return 1;
}

// ====== kernel_console_init ======

int kernel_console_init(KERNEL_VIDEO_CONSOLE *console, uint32_t columns, uint32_t rows)
{
    uint32_t index;
    if (!console || kernel_console_plan(columns, rows, 0, 0, 13) == KERNEL_CONSOLE_INVALID) return 0;
    console->Self = (uintptr_t)console;
    console->Columns = columns;
    console->Rows = rows;
    console->X = console->Y = 0;
    console->Caret = UINT32_MAX;
    for (index = 0; index < KERNEL_CONSOLE_CELLS; ++index) console->Cells[index] = 0;
    for (index = 0; index < KERNEL_CONSOLE_DIRTY; ++index) console->Dirty[index] = 0;
    return kernel_console_clear(console);
}

// ====== scroll ======

static void scroll(KERNEL_VIDEO_CONSOLE *console)
{
    uint32_t index, count = console->Columns * console->Rows;
    for (index = 0; index < count; ++index) {
        console->Cells[index] = index < count - console->Columns ?
            console->Cells[index + console->Columns] : 32;
        mark(console, index);
    }
}

// ====== kernel_console_step ======

int kernel_console_step(KERNEL_VIDEO_CONSOLE *console, uint32_t scalar)
{
    uint64_t plan;
    uint32_t x, y, action, index;
    if (!valid(console)) return 0;
    plan = kernel_console_plan(console->Columns, console->Rows, console->X, console->Y, scalar);
    if (plan == KERNEL_CONSOLE_INVALID) return 0;
    x = (uint32_t)(plan & 65535);
    y = (uint32_t)((plan >> 16) & 65535);
    action = (uint32_t)(plan >> 32);
    if (x > console->Columns || y >= console->Rows || action > 7 ||
        ((action & 1) && !x) || ((action & 2) && x >= console->Columns)) return 0;
    if (action & 4) scroll(console);
    if (action & 3) {
        index = y * console->Columns + x - ((action & 1) ? 1U : 0U);
        console->Cells[index] = (action & 1) ? scalar : 32;
        mark(console, index);
    }
    console->X = x;
    console->Y = y;
    return 1;
}

// ====== kernel_console_peek ======

int kernel_console_peek(const KERNEL_VIDEO_CONSOLE *console, uint32_t start,
    uint32_t *index, uint32_t *scalar)
{
    uint32_t current, count;
    if (!valid(console) || !index || !scalar) return -1;
    count = console->Columns * console->Rows;
    if (start > count) return -1;
    for (current = start; current < count; ++current) {
        if (console->Dirty[current / 8] & (1U << (current % 8))) {
            *index = current;
            *scalar = console->Cells[current];
            return 1;
        }
    }
    return 0;
}

// ====== kernel_console_ack ======

int kernel_console_ack(KERNEL_VIDEO_CONSOLE *console, uint32_t index, uint32_t scalar)
{
    if (!valid(console) || index >= console->Columns * console->Rows ||
        console->Cells[index] != scalar ||
        !(console->Dirty[index / 8] & (1U << (index % 8)))) return 0;
    console->Dirty[index / 8] &= (uint8_t)~(1U << (index % 8));
    return 1;
}

// ====== kernel_console_caret ======

int kernel_console_caret(KERNEL_VIDEO_CONSOLE *console, uint32_t enabled)
{
    uint32_t next;
    if (!valid(console) || enabled > 1) return 0;
    next = enabled ? kernel_console_caret_index(console->Columns, console->Rows,
        console->X, console->Y) : UINT32_MAX;
    if (enabled && next == UINT32_MAX) return 0;
    if (next == console->Caret) return 1;
    if (console->Caret != UINT32_MAX) mark(console, console->Caret);
    console->Caret = next;
    if (next != UINT32_MAX) mark(console, next);
    return 1;
}
