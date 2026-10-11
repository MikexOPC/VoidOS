#include "probe.h"
#include "../../../../../kernel/console/video/console.h"
#include "kernel/diagnostics/log.h"
static KERNEL_VIDEO_CONSOLE console;

// ====== kernel_console_grid_probe ======

int kernel_console_grid_probe(void)
{
    uint32_t cycle, index, scalar, count;
    for (cycle = 0; cycle < 100; ++cycle) {
        if (!kernel_console_init(&console, 3, 2) ||
            !kernel_console_step(&console, 0xe8) || !kernel_console_step(&console, 0x20ac) ||
            !kernel_console_step(&console, 'A') || !kernel_console_step(&console, 'B') ||
            !kernel_console_step(&console, 8) || !kernel_console_step(&console, 'C') ||
            !kernel_console_step(&console, 10) || console.Cells[0] != 'C' ||
            console.Cells[3] != 32 || console.X != 0 || console.Y != 1 ||
            kernel_console_step(&console, 0xd800)) return 0;
        count = 0;
        while (kernel_console_peek(&console, count, &index, &scalar) == 1) {
            if (index != count || !kernel_console_ack(&console, index, scalar)) return 0;
            ++count;
        }
        if (count != 6 || kernel_console_peek(&console, 0, &index, &scalar) != 0) return 0;
    }
    if (!kernel_console_init(&console, 240, 67)) return 0;
    for (index = 0; index < KERNEL_CONSOLE_CELLS + 1; ++index) {
        if (!kernel_console_step(&console, 32 + index % 95)) return 0;
    }
    for (index = 0; index < KERNEL_CONSOLE_CELLS - 240; ++index) {
        if (console.Cells[index] != 32 + (index + 240) % 95) return 0;
    }
    return kernel_console_service_probe() && kernel_log(0, "KERNEL_CONSOLE_GRID_OK");
}
