#include "../../measurement/budget.h"
#include <stdio.h>
#include <stdlib.h>
static KERNEL_VIDEO_CONSOLE console;
static KERNEL_CONSOLE_RENDER render;
static KERNEL_CONSOLE_MEASUREMENT owner, copy;
static uint32_t reads, draws, ticks, start, reenter, failure, raw;
#define CHECK(value) do { if (!(value)) { \
    fprintf(stderr, "FAIL measure line %d\n", __LINE__); exit(1); } } while (0)

// ====== clock_read ======

static uint32_t clock_read(void *context)
{
    CHECK(context == &reads);
    if (reenter) CHECK(!kernel_console_measure_step(&owner));
    return reads++ & 1 ? (raw ? 0x1000000U : (start + ticks) & owner.Mask) : start;
}

// ====== draw ======

static int draw(void *context, uint32_t column, uint32_t row, uint32_t scalar, uint64_t *duration)
{
    CHECK(context == &draws && column < 2 && !row && scalar == 32);
    ++draws; *duration = 1;
    return !failure;
}

// ====== reset ======

static void reset(void)
{
    reads = draws = reenter = failure = raw = start = 0; ticks = 100;
    CHECK(kernel_console_init(&console, 2, 1));
    CHECK(kernel_console_render_init(&render, &console, draw, &draws));
    CHECK(kernel_console_measure_init(&owner, &render, clock_read, &reads, 0xffffff));
}

// ====== kernel_console_measure_test ======

void kernel_console_measure_test(void)
{
    uint32_t index, scalar;
    reset();
    CHECK(!kernel_console_measure_init(NULL, &render, clock_read, &reads, 0xffffff));
    CHECK(!kernel_console_measure_init(&copy, NULL, clock_read, &reads, 0xffffff));
    CHECK(!kernel_console_measure_init(&copy, &render, NULL, &reads, 0xffffff));
    CHECK(!kernel_console_measure_init(&copy, &render, clock_read, &reads, 0xffff));
    CHECK(!kernel_console_measure_init((KERNEL_CONSOLE_MEASUREMENT *)&console,
        &render, clock_read, &reads, 0xffffff));
    copy = owner; CHECK(!kernel_console_measure_step(&copy));
    reenter = 1;
    CHECK(kernel_console_measure_step(&owner) == KERNEL_CONSOLE_RENDER_MORE);
    CHECK(owner.Cells == 1 && owner.MaxTicks == 100 && draws == 1 && reads == 2 && !owner.Busy);
    start = 0xfffff9; ticks = 10;
    CHECK(kernel_console_measure_step(&owner) == KERNEL_CONSOLE_RENDER_IDLE);
    CHECK(owner.Cells == 2 && owner.LastTicks == 10 && owner.MaxTicks == 100);
    CHECK(kernel_console_measure_step(&owner) == KERNEL_CONSOLE_RENDER_IDLE && reads == 4);
    reset(); ticks = kernel_console_pm_limit();
    CHECK(kernel_console_measure_step(&owner) == KERNEL_CONSOLE_RENDER_MORE);
    CHECK(owner.SlowCells == 1);
    reset(); ticks = kernel_console_pm_goal();
    CHECK(kernel_console_measure_step(&owner) == KERNEL_CONSOLE_RENDER_MORE && !owner.SlowCells);
    reset(); ticks = kernel_console_pm_goal() + 1;
    CHECK(kernel_console_measure_step(&owner) == KERNEL_CONSOLE_RENDER_MORE && owner.SlowCells == 1);
    reset(); ticks = kernel_console_pm_limit() + 1;
    CHECK(!kernel_console_measure_step(&owner) && owner.Failed && !owner.Busy);
    CHECK(owner.LastTicks == ticks && owner.Cells == 0 && render.CellsDone == 1);
    CHECK(!kernel_console_measure_step(&owner) && draws == 1 && reads == 2);
    reset(); ticks = 0;
    CHECK(!kernel_console_measure_step(&owner) && owner.Failed);
    reset(); raw = 1;
    CHECK(!kernel_console_measure_step(&owner) && owner.Failed);
    reset(); failure = 1;
    CHECK(!kernel_console_measure_step(&owner) && owner.Failed && !owner.Cells);
    CHECK(kernel_console_peek(&console, 0, &index, &scalar) == 1 && index == 0);
    reset(); owner.Cells = UINT64_MAX;
    CHECK(!kernel_console_measure_step(&owner) && !draws && !reads);
    reset(); owner.SlowCells = 1;
    CHECK(!kernel_console_measure_step(&owner) && !draws && !reads);
    printf("PASS measured flush: single cell, wrap, timing limits, stopped clock, ownership/reentry\n");
}
