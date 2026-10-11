#include "render.h"

// ====== separate ======

static int separate(const void *a, size_t bytes, const void *b, size_t other)
{
    uintptr_t first = (uintptr_t)a, second = (uintptr_t)b;
    return first <= UINTPTR_MAX - bytes && second <= UINTPTR_MAX - other &&
        (first + bytes <= second || second + other <= first);
}

// ====== kernel_console_batch_init ======

int kernel_console_batch_init(KERNEL_CONSOLE_BATCH *owner, KERNEL_CONSOLE_RENDER *render,
    KERNEL_CONSOLE_GROUP_DRAW draw, void *context)
{
    uint32_t index, scalar;
    if (!owner || (uintptr_t)owner % _Alignof(KERNEL_CONSOLE_BATCH) || !render ||
        (uintptr_t)render % _Alignof(KERNEL_CONSOLE_RENDER) ||
        render->Self != (uintptr_t)render || !render->Draw || render->Busy || !draw ||
        kernel_console_peek(render->Console, 0, &index, &scalar) < 0 ||
        !separate(owner, sizeof(*owner), render, sizeof(*render)) ||
        !separate(owner, sizeof(*owner), render->Console, sizeof(*render->Console))) return 0;
    owner->Self = (uintptr_t)owner; owner->Render = render;
    owner->Draw = draw; owner->Context = context;
    owner->Groups = 0; owner->Busy = owner->Failed = 0;
    return 1;
}

// ====== kernel_console_batch_flush ======

uint32_t kernel_console_batch_flush(KERNEL_CONSOLE_BATCH *owner, uint32_t budget)
{
    KERNEL_CONSOLE_RENDER *render;
    KERNEL_VIDEO_CONSOLE *console;
    uint32_t index, scalar, next, count, offset, captured[KERNEL_CONSOLE_BATCH_LIMIT], result = 0;
    uint32_t columns, rows, start;
    uint64_t ticks = 0, cells, groups;

    int pending;

    if (!owner || (uintptr_t)owner % _Alignof(KERNEL_CONSOLE_BATCH) ||
        owner->Self != (uintptr_t)owner || owner->Busy || owner->Failed || !owner->Draw ||
        !(render = owner->Render) || (uintptr_t)render % _Alignof(KERNEL_CONSOLE_RENDER) ||
        render->Self != (uintptr_t)render || !render->Draw ||
        render->Busy || !budget || budget > KERNEL_CONSOLE_BATCH_LIMIT) return 0;
    pending = kernel_console_peek(render->Console, render->Next, &index, &scalar);
    if (!pending && render->Next) pending = kernel_console_peek(render->Console, 0, &index, &scalar);
    if (pending < 0) { owner->Failed = 1; return 0; }
    if (!pending) return KERNEL_CONSOLE_RENDER_IDLE;
    console = render->Console; columns = console->Columns; rows = console->Rows;
    cells = render->CellsDone; groups = owner->Groups; start = render->Next;
    captured[0] = scalar;
    for (count = 1; count < budget && count < render->Console->Columns - index % render->Console->Columns; ++count) {
        pending = kernel_console_peek(render->Console, index + count, &next, &scalar);
        if (pending < 0) { owner->Failed = 1; return 0; }
        if (!pending || next != index + count) break;
        captured[count] = scalar;
    }
    if (owner->Groups == UINT64_MAX || render->CellsDone > UINT64_MAX - count) {
        owner->Failed = 1; return 0;
    }
    owner->Busy = render->Busy = 1;
    if (!owner->Draw(owner->Context, index % render->Console->Columns,
        index / render->Console->Columns, captured, count, &ticks) || !ticks) goto finish;
    if (owner->Self != (uintptr_t)owner || owner->Render != render || owner->Groups != groups ||
        owner->Failed || owner->Busy != 1 || render->Busy != 1 ||
        render->Self != (uintptr_t)render || render->Console != console || render->CellsDone != cells ||
        render->Next != start || console->Columns != columns || console->Rows != rows) goto finish;
    for (offset = 0; offset < count; ++offset)
        if (kernel_console_peek(render->Console, index + offset, &next, &scalar) != 1 ||
            next != index + offset || scalar != captured[offset]) goto finish;
    for (offset = 0; offset < count; ++offset)
        if (!kernel_console_ack(render->Console, index + offset, captured[offset])) goto finish;
    render->CellsDone += count; ++owner->Groups;
    if (ticks > render->MaxTicks) render->MaxTicks = ticks;
    render->Next = index + count;
    pending = kernel_console_peek(render->Console, render->Next, &next, &scalar);
    if (!pending && render->Next) pending = kernel_console_peek(render->Console, 0, &next, &scalar);
    if (pending >= 0) result = pending ? KERNEL_CONSOLE_RENDER_MORE : KERNEL_CONSOLE_RENDER_IDLE;
finish:
    if (!result) owner->Failed = 1;
    owner->Busy = render->Busy = 0;
    return result;
}
