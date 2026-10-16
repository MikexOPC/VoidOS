#include "budget.h"

// ====== separate ======

static int separate(const void *first, size_t bytes, const void *second, size_t other)
{
    uintptr_t a = (uintptr_t)first, b = (uintptr_t)second;
    return a <= UINTPTR_MAX - bytes && b <= UINTPTR_MAX - other &&
        (a + bytes <= b || b + other <= a);
}

// ====== kernel_console_measure_init ======

int kernel_console_measure_init(KERNEL_CONSOLE_MEASUREMENT *owner, KERNEL_CONSOLE_RENDER *render,
    KERNEL_CONSOLE_CLOCK read, void *context, uint32_t mask)
{
    uint32_t index, scalar;
    if (!owner || (uintptr_t)owner % _Alignof(KERNEL_CONSOLE_MEASUREMENT) ||
        !render || render->Self != (uintptr_t)render || !render->Draw || !read ||
        render->Busy || kernel_console_peek(render->Console, 0, &index, &scalar) < 0 ||
        !separate(owner, sizeof(*owner), render, sizeof(*render)) ||
        !separate(owner, sizeof(*owner), render->Console, sizeof(*render->Console)) ||
        kernel_console_pm_sample(0, 1, mask) != (UINT64_C(1) << 32 | 1)) return 0;
    owner->Self = (uintptr_t)owner;
    owner->Render = render; owner->Read = read; owner->Context = context;
    owner->Mask = mask; owner->Cells = owner->SlowCells = owner->Overruns = 0;
    owner->LastTicks = owner->MaxTicks = owner->Busy = owner->Failed = 0;
    return 1;
}

// ====== measure ======

static uint32_t measure(KERNEL_CONSOLE_MEASUREMENT *owner, KERNEL_CONSOLE_BATCH *batch, uint32_t live)
{
    uint32_t index, scalar, start, end, result = 0;
    uint64_t before, sample, cells;
    int pending;
    if (!owner || owner->Self != (uintptr_t)owner || !owner->Render || !owner->Read ||
        owner->Busy || owner->Failed || owner->Render->Self != (uintptr_t)owner->Render ||
        owner->Render->Busy || owner->Cells == UINT64_MAX || owner->SlowCells > owner->Cells ||
        owner->Overruns > owner->Cells || owner->Overruns == UINT64_MAX) return 0;
    if (batch && ((uintptr_t)batch % _Alignof(KERNEL_CONSOLE_BATCH) || batch->Self != (uintptr_t)batch ||
        batch->Render != owner->Render || batch->Busy || batch->Failed)) return 0;
    pending = kernel_console_peek(owner->Render->Console, 0, &index, &scalar);
    if (pending < 0) return 0;
    if (!pending) return KERNEL_CONSOLE_RENDER_IDLE;
    owner->Busy = 1;
    before = owner->Render->CellsDone;
    start = owner->Read(owner->Context);
    result = batch ? kernel_console_batch_flush(batch, KERNEL_CONSOLE_BATCH_LIMIT) :
        kernel_console_render_flush(owner->Render, 1);
    end = owner->Read(owner->Context);
    sample = kernel_console_pm_sample(start, end, owner->Mask);
    if (sample != UINT64_MAX) owner->LastTicks = (uint32_t)sample;
    if (owner->LastTicks > owner->MaxTicks) owner->MaxTicks = owner->LastTicks;
    cells = owner->Render->CellsDone >= before ? owner->Render->CellsDone - before : 0;
    if (!result || sample == UINT64_MAX ||
        (sample >> 32 != 1 && !(live && sample >> 32 == 3)) || !cells ||
        cells > (batch ? KERNEL_CONSOLE_BATCH_LIMIT : 1U) || owner->Cells > UINT64_MAX - cells) {
        owner->Failed = 1;
        result = 0;
    } else {
        owner->Cells += cells;
        if (owner->LastTicks > kernel_console_pm_goal()) owner->SlowCells += cells;
        if (sample >> 32 == 3) ++owner->Overruns;
    }
    owner->Busy = 0;
    return result;
}

// ====== kernel_console_measure_step ======

uint32_t kernel_console_measure_step(KERNEL_CONSOLE_MEASUREMENT *owner)
{
    return measure(owner, NULL, 0);
}

// ====== kernel_console_measure_batch_step ======

uint32_t kernel_console_measure_batch_step(KERNEL_CONSOLE_MEASUREMENT *owner, KERNEL_CONSOLE_BATCH *batch)
{
    return batch ? measure(owner, batch, 0) : 0;
}

// ====== kernel_console_measure_batch_step_live ======

uint32_t kernel_console_measure_batch_step_live(KERNEL_CONSOLE_MEASUREMENT *owner, KERNEL_CONSOLE_BATCH *batch)
{
    return batch ? measure(owner, batch, 1) : 0;
}
