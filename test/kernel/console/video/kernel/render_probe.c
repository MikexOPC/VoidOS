#include "probe.h"
#include "../../../../../kernel/console/video/render.h"
#include "arch/x86/kernel/drivers/video/framebuffer/integration/kernel.h"
#include "arch/x86/kernel/drivers/video/framebuffer/gateway/cell.h"
#include "kernel/diagnostics/log.h"
static KERNEL_VIDEO_CONSOLE console;
static KERNEL_CONSOLE_RENDER render;
#define APPEND(literal) append((literal), (uint32_t)(sizeof(literal) - 1U))

// ====== draw ======

static int draw(void *context, uint32_t column, uint32_t row, uint32_t scalar, uint64_t *ticks)
{
    if (context != &render) return 0;
    return x86_fb_kernel_cell(column + 2, row + 10, scalar, 0xffffff, 0x081018, ticks);
}

// ====== append ======

static int append(const char *text, uint32_t bytes)
{
    uint32_t index;
    if (!text || bytes > 24) return 0;
    for (index = 0; index < bytes; ++index) {
        if (!kernel_console_step(&console, (unsigned char)text[index])) return 0;
    }
    return 1;
}

// ====== kernel_console_render_probe ======

int kernel_console_render_probe(void)
{
    uint64_t flags, actual, ticks = 0;
    uint32_t result, batches = 0;
    int valid = 0;
    flags = x86_fb_cell_suspend();
    if (flags & 512) goto finish;
    if (!kernel_console_init(&console, 24, 3) || !APPEND("MikeSW console\nciao ") ||
        !kernel_console_step(&console, 0xe8) || !APPEND(" ") ||
        !kernel_console_step(&console, 0x20ac) || !APPEND("\nprova AB") ||
        !kernel_console_step(&console, 8) || !APPEND("C\nTutto OK") ||
        !kernel_console_step(&console, 0x1f642) ||
        !kernel_console_render_init(&render, &console, draw, &render) ||
        kernel_console_render_flush(&render, 16) != KERNEL_CONSOLE_RENDER_MORE) goto finish;
    actual = x86_fb_cell_suspend();
    if (actual & 512 || !x86_fb_cell_resume(flags | 512)) goto finish;
    if (x86_fb_kernel_cell(240, 0, 'A', 0, 0, &ticks)) goto finish;
    do {
        result = kernel_console_render_flush(&render, 16);
        actual = x86_fb_cell_suspend();
        if (!(actual & 512) || !result || ++batches > 5) goto finish;
        if (result == KERNEL_CONSOLE_RENDER_MORE && !x86_fb_cell_resume(flags | 512)) goto finish;
    } while (result == KERNEL_CONSOLE_RENDER_MORE);
    valid = render.CellsDone == 72 && render.MaxTicks != 0 && !render.Busy;
finish:
    if (!x86_fb_cell_resume(flags) || !valid) return 0;
    return kernel_text("KERNEL_CONSOLE_RENDER_CELLS: ") && kernel_number(render.CellsDone) &&
        kernel_text("\nKERNEL_CONSOLE_RENDER_IF1_CELLS: ") && kernel_number(render.CellsDone - 16) &&
        kernel_text("\nKERNEL_CONSOLE_RENDER_TSC_MAX: ") && kernel_number(render.MaxTicks) &&
        kernel_text("\n") && kernel_log(0, "KERNEL_CONSOLE_RENDER_OK");
}
