#include "../../render.h"
#include <stdio.h>
#include <stdlib.h>
static KERNEL_VIDEO_CONSOLE console;
static KERNEL_CONSOLE_RENDER render, copy;
static uint32_t calls, fail_at, reenter;
#define CHECK(value) do { if (!(value)) { \
    fprintf(stderr, "FAIL render line %d\n", __LINE__); exit(1); } } while (0)

// ====== draw ======

static int draw(void *context, uint32_t column, uint32_t row, uint32_t scalar, uint64_t *ticks)
{
    CHECK(context == &calls && column < console.Columns && row < console.Rows);
    CHECK(console.Cells[row * console.Columns + column] == scalar);
    if (reenter) CHECK(!kernel_console_render_flush(&render, 1));
    ++calls;
    *ticks = calls;
    return calls != fail_at;
}

// ====== kernel_console_render_test ======

void kernel_console_render_test(void)
{
    uint32_t index, scalar;
    CHECK(kernel_console_init(&console, 4, 2));
    CHECK(kernel_console_render_init(&render, &console, draw, &calls));
    CHECK(!kernel_console_render_init(NULL, &console, draw, &calls));
    CHECK(!kernel_console_render_init(&copy, &console, NULL, &calls));
    CHECK(!kernel_console_render_init(&copy, NULL, draw, &calls));
    copy = render;
    CHECK(!kernel_console_render_flush(&copy, 1));
    CHECK(!kernel_console_render_flush(&render, 0));
    CHECK(!kernel_console_render_flush(&render, 17));
    reenter = 1;
    CHECK(kernel_console_render_flush(&render, 3) == KERNEL_CONSOLE_RENDER_MORE);
    CHECK(calls == 3 && render.CellsDone == 3 && render.MaxTicks == 3 && !render.Busy);
    fail_at = 5;
    CHECK(!kernel_console_render_flush(&render, 3));
    CHECK(calls == 5 && render.CellsDone == 4 && !render.Busy);
    CHECK(kernel_console_peek(&console, 0, &index, &scalar) == 1 && index == 4);
    fail_at = 0;
    CHECK(kernel_console_render_flush(&render, 16) == KERNEL_CONSOLE_RENDER_IDLE);
    CHECK(render.CellsDone == 8 && render.MaxTicks == 9);
    CHECK(kernel_console_render_flush(&render, 1) == KERNEL_CONSOLE_RENDER_IDLE && calls == 9);
    CHECK(kernel_console_step(&console, 'A'));
    CHECK(kernel_console_render_flush(&render, 1) == KERNEL_CONSOLE_RENDER_IDLE);
    CHECK(render.Next == 1 && render.CellsDone == 9);
    CHECK(kernel_console_clear(&console));
    render.CellsDone = UINT64_MAX;
    CHECK(!kernel_console_render_flush(&render, 1) && !render.Busy && calls == 10);
    render.CellsDone = 0;
    render.Next = UINT32_MAX;
    CHECK(!kernel_console_render_flush(&render, 1) && !render.Busy && calls == 10);
    render.Next = 0;
    CHECK(kernel_console_render_flush(&render, 16) == KERNEL_CONSOLE_RENDER_IDLE);
    CHECK(render.CellsDone == 8);
    printf("PASS render budget/failure/retry/reentry/ownership/wrap\n");
}
