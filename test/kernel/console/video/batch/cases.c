#include "../../../../../kernel/console/video/batch/render.h"
#include "../../../../../kernel/console/video/measurement/budget.h"
#include <assert.h>
#include <stdio.h>
static KERNEL_VIDEO_CONSOLE console;
static KERNEL_CONSOLE_RENDER render;
static KERNEL_CONSOLE_BATCH batch;
static KERNEL_CONSOLE_MEASUREMENT measured;
static uint32_t calls, seen, mode, clock_value;

// ====== single ======

static int single(void *context, uint32_t column, uint32_t row, uint32_t scalar, uint64_t *ticks)
{
    (void)context; (void)column; (void)row; (void)scalar; (void)ticks;
    assert(0); return 0;
}

// ====== draw ======

static int draw(void *context, uint32_t column, uint32_t row,
    const uint32_t *scalars, uint32_t count, uint64_t *ticks)
{
    uint32_t index;
    assert(context == &batch && count >= 1 && count <= 8 && column + count <= 8 && row < 2);
    assert(batch.Busy && render.Busy);
    assert(!kernel_console_batch_flush(&batch, 8) && !kernel_console_render_flush(&render, 1));
    for (index = 0; index < count; ++index) assert(scalars[index] == console.Cells[row * 8 + column + index]);
    ++calls; seen += count; *ticks = mode == 2 ? 0 : 100;
    if (mode == 3) ++console.Cells[row * 8 + column];
    if (mode == 4) console.Columns = 7;
    return mode != 1;
}

// ====== clock_read ======

static uint32_t clock_read(void *context)
{
    assert(context == &measured); clock_value += 100; return clock_value;
}

// ====== init ======

static void init(void)
{
    uint32_t index;
    assert(kernel_console_init(&console, 8, 2));
    for (index = 0; index < 16; ++index) console.Cells[index] = 65 + index;
    assert(kernel_console_render_init(&render, &console, single, NULL));
    assert(kernel_console_batch_init(&batch, &render, draw, &batch));
    assert(kernel_console_measure_init(&measured, &render, clock_read, &measured, 0xffffff));
    calls = seen = mode = clock_value = 0;
}

// ====== main ======

int main(void)
{
    uint32_t mask, budget, result, error;
    KERNEL_CONSOLE_BATCH copy;
    _Alignas(KERNEL_CONSOLE_RENDER) unsigned char unaligned[sizeof(KERNEL_CONSOLE_RENDER) + 1];
    init();
    assert(!kernel_console_batch_init(&batch, (KERNEL_CONSOLE_RENDER *)(unaligned + 1), draw, &batch));
    batch.Render = (KERNEL_CONSOLE_RENDER *)(unaligned + 1);
    assert(!kernel_console_batch_flush(&batch, 8));
    for (budget = 1; budget <= 8; ++budget) for (mask = 0; mask < 256; ++mask) {
        init();
        console.Dirty[0] = (uint8_t)mask; console.Dirty[1] = (uint8_t)(mask ^ 255);
        do { result = kernel_console_batch_flush(&batch, budget); assert(result && !batch.Failed); }
        while (result == KERNEL_CONSOLE_RENDER_MORE);
        assert(seen == 8 && render.CellsDone == 8 && batch.Groups == calls);
        assert(!console.Dirty[0] && !console.Dirty[1]);
        assert(kernel_console_batch_flush(&batch, 8) == KERNEL_CONSOLE_RENDER_IDLE && seen == 8);
    }
    for (error = 1; error <= 4; ++error) {
        init(); mode = error;
        assert(!kernel_console_batch_flush(&batch, 8) && batch.Failed);
        assert(console.Dirty[0] == 255 && console.Dirty[1] == 255 && !render.CellsDone && !batch.Groups);
        mode = 0; assert(!kernel_console_batch_flush(&batch, 8) && calls == 1);
        assert(!batch.Busy && !render.Busy);
    }
    init(); copy = batch; assert(!kernel_console_batch_flush(&copy, 8));
    assert(!kernel_console_batch_flush(&batch, 0) && !kernel_console_batch_flush(&batch, 9));
    batch.Groups = UINT64_MAX;
    assert(!kernel_console_batch_flush(&batch, 8) && !calls && console.Dirty[0] == 255);
    init(); render.CellsDone = UINT64_MAX - 7;
    assert(!kernel_console_batch_flush(&batch, 8) && !calls);
    init();
    assert(kernel_console_measure_batch_step(&measured, &batch) == KERNEL_CONSOLE_RENDER_MORE);
    assert(measured.Cells == 8 && batch.Groups == 1 && measured.LastTicks == 100);
    assert(kernel_console_measure_batch_step(&measured, &batch) == KERNEL_CONSOLE_RENDER_IDLE);
    assert(measured.Cells == 16 && batch.Groups == 2 && measured.LastTicks == 100);
    assert(kernel_console_measure_batch_step(&measured, &batch) == KERNEL_CONSOLE_RENDER_IDLE && clock_value == 400);
    init(); mode = 1;
    assert(!kernel_console_measure_batch_step(&measured, &batch) && measured.Failed && batch.Failed);
    assert(!measured.Cells && console.Dirty[0] == 255);
    puts("PASS batch: 2048 sparse workloads, row/gap/budget bounds, reentry, terminal failures and accounting");
    return 0;
}
