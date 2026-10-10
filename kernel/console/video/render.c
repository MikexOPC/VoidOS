#include "render.h"

// ====== kernel_console_render_init ======

int kernel_console_render_init(KERNEL_CONSOLE_RENDER *render, KERNEL_VIDEO_CONSOLE *console,
    KERNEL_CONSOLE_DRAW draw, void *context)
{
    uint32_t index, scalar;
    if (!render || !draw || kernel_console_peek(console, 0, &index, &scalar) < 0) return 0;
    render->Self = (uintptr_t)render;
    render->Console = console;
    render->Draw = draw;
    render->Context = context;
    render->CellsDone = render->MaxTicks = 0;
    render->Next = render->Busy = 0;
    return 1;
}

// ====== pending ======

static int pending(KERNEL_CONSOLE_RENDER *render, uint32_t *index, uint32_t *scalar)
{
    int result = kernel_console_peek(render->Console, render->Next, index, scalar);
    if (result == 0 && render->Next) result = kernel_console_peek(render->Console, 0, index, scalar);
    return result;
}

// ====== kernel_console_render_flush ======

uint32_t kernel_console_render_flush(KERNEL_CONSOLE_RENDER *render, uint32_t budget)
{
    uint32_t index, scalar, done, result = 0;
    int available;
    if (!render || render->Self != (uintptr_t)render || !render->Draw || render->Busy ||
        !budget || budget > KERNEL_CONSOLE_RENDER_BUDGET) return 0;
    render->Busy = 1;
    for (done = 0; done < budget; ++done) {
        uint64_t ticks = 0;
        available = pending(render, &index, &scalar);
        if (available < 0) goto finish;
        if (!available) { result = KERNEL_CONSOLE_RENDER_IDLE; goto finish; }
        if (render->CellsDone == UINT64_MAX ||
            !render->Draw(render->Context, index % render->Console->Columns,
                index / render->Console->Columns, scalar, &ticks) || !ticks ||
            !kernel_console_ack(render->Console, index, scalar)) goto finish;
        ++render->CellsDone;
        if (ticks > render->MaxTicks) render->MaxTicks = ticks;
        render->Next = index + 1;
    }
    available = pending(render, &index, &scalar);
    if (available >= 0) result = available ? KERNEL_CONSOLE_RENDER_MORE : KERNEL_CONSOLE_RENDER_IDLE;
finish:
    render->Busy = 0;
    return result;
}
