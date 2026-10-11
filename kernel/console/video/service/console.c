#include "console.h"

// ====== separate ======

static int separate(uintptr_t first, uint32_t size, uintptr_t second, uint32_t extent)
{
    if (first > UINTPTR_MAX - size || second > UINTPTR_MAX - extent) return 0;
    return first + size <= second || second + extent <= first;
}

// ====== valid ======

static int valid(const KERNEL_CONSOLE_SERVICE *owner)
{
    const KERNEL_VIDEO_CONSOLE *view, *base;
    if (!owner || owner->Self != (uintptr_t)owner || !(view = owner->View) ||
        !separate((uintptr_t)owner, sizeof(*owner), (uintptr_t)view, sizeof(*view)) ||
        owner->Length > KERNEL_CONSOLE_TEXT_LIMIT || owner->Closed > 1 || owner->Failed > 1 ||
        owner->Busy > 2) return 0;
    base = &owner->Transcript;
    return view->Self == (uintptr_t)view && base->Self == (uintptr_t)base &&
        view->Columns == base->Columns && view->Rows == base->Rows &&
        kernel_console_plan(view->Columns, view->Rows, view->X, view->Y, 13) !=
            KERNEL_CONSOLE_INVALID &&
        kernel_console_plan(base->Columns, base->Rows, base->X, base->Y, 13) !=
            KERNEL_CONSOLE_INVALID;
}

// ====== enter ======

static int enter(KERNEL_CONSOLE_SERVICE *owner)
{
    if (!valid(owner) || owner->Busy || owner->Closed || owner->Failed) return 0;
    owner->Busy = 1;
    return 1;
}

// ====== finish ======

static int finish(KERNEL_CONSOLE_SERVICE *owner, int success)
{
    owner->Busy = 0;
    if (!success) owner->Failed = 1;
    return success;
}

// ====== decode ======

static int decode(const KERNEL_CONSOLE_SERVICE *owner, const char *data,
    uint32_t length, uint32_t capacity, uint32_t controls, uint32_t *scalars, uint32_t *count)
{
    uint32_t offset = 0;
    if (!data || capacity < length || length > KERNEL_CONSOLE_TEXT_LIMIT ||
        !separate((uintptr_t)data, length, (uintptr_t)owner, sizeof(*owner)) ||
        !separate((uintptr_t)data, length, (uintptr_t)owner->View, sizeof(*owner->View))) return 0;
    *count = 0;
    while (offset < length) {
        uint32_t available = length - offset, packed = 0, consumed;
        uint64_t decoded;
        if (available > 4) available = 4;
        for (uint32_t index = 0; index < available; ++index)
            packed |= (uint32_t)(uint8_t)data[offset + index] << (index * 8);
        decoded = kernel_console_utf8(packed, available, controls);
        consumed = (uint32_t)(decoded >> 32);
        if (decoded == UINT64_MAX || !consumed || consumed > available || *count >= 256) return 0;
        scalars[(*count)++] = (uint32_t)decoded;
        offset += consumed;
    }
    return 1;
}

// ====== compose ======

static int compose(KERNEL_CONSOLE_SERVICE *owner)
{
    KERNEL_VIDEO_CONSOLE *view = owner->View;
    uint32_t count = view->Columns * view->Rows;
    for (uint32_t index = 0; index < count; ++index) {
        uint32_t scalar = owner->Transcript.Cells[index];
        if (view->Cells[index] != scalar) {
            view->Cells[index] = scalar;
            view->Dirty[index / 8] |= (uint8_t)(1U << (index % 8));
        }
    }
    view->X = owner->Transcript.X;
    view->Y = owner->Transcript.Y;
    if (!kernel_console_step(view, '>') || !kernel_console_step(view, ' ')) return 0;
    for (uint32_t index = 0; index < owner->Length; ++index)
        if (!kernel_console_step(view, owner->Line[index])) return 0;
    return 1;
}

// ====== kernel_console_service_init ======

int kernel_console_service_init(KERNEL_CONSOLE_SERVICE *owner, KERNEL_VIDEO_CONSOLE *view,
    uint32_t columns, uint32_t rows)
{
    if (!owner || !view ||
        !separate((uintptr_t)owner, sizeof(*owner), (uintptr_t)view, sizeof(*view)) ||
        kernel_console_plan(columns, rows, 0, 0, 13) == KERNEL_CONSOLE_INVALID) return 0;
    owner->Self = (uintptr_t)owner;
    owner->View = view;
    owner->Length = owner->Busy = owner->Closed = owner->Failed = 0;
    for (uint32_t index = 0; index < KERNEL_CONSOLE_TEXT_LIMIT; ++index) owner->Line[index] = 0;
    if (!kernel_console_init(&owner->Transcript, columns, rows) ||
        !kernel_console_init(view, columns, rows)) return finish(owner, 0);
    return finish(owner, compose(owner));
}

// ====== kernel_console_service_edit ======

int kernel_console_service_edit(KERNEL_CONSOLE_SERVICE *owner,
    const char *data, uint32_t length, uint32_t capacity)
{
    uint32_t scalars[KERNEL_CONSOLE_TEXT_LIMIT], count;
    if (!valid(owner) || owner->Busy || owner->Closed || owner->Failed ||
        !decode(owner, data, length, capacity, 0, scalars, &count) || !enter(owner)) return 0;
    for (uint32_t index = 0; index < KERNEL_CONSOLE_TEXT_LIMIT; ++index)
        owner->Line[index] = index < count ? scalars[index] : 0;
    owner->Length = count;
    return finish(owner, compose(owner));
}

// ====== kernel_console_service_write ======

int kernel_console_service_write(KERNEL_CONSOLE_SERVICE *owner,
    const char *data, uint32_t length, uint32_t capacity)
{
    uint32_t scalars[KERNEL_CONSOLE_TEXT_LIMIT], count;
    if (!valid(owner) || owner->Busy || owner->Closed || owner->Failed ||
        !decode(owner, data, length, capacity, 1, scalars, &count) || !enter(owner)) return 0;
    for (uint32_t index = 0; index < count; ++index)
        if (!kernel_console_step(&owner->Transcript, scalars[index])) return finish(owner, 0);
    return finish(owner, compose(owner));
}

// ====== kernel_console_service_submit ======

int kernel_console_service_submit(KERNEL_CONSOLE_SERVICE *owner)
{
    if (!enter(owner)) return 0;
    if (!kernel_console_step(&owner->Transcript, '>') ||
        !kernel_console_step(&owner->Transcript, ' ')) return finish(owner, 0);
    for (uint32_t index = 0; index < owner->Length; ++index)
        if (!kernel_console_step(&owner->Transcript, owner->Line[index])) return finish(owner, 0);
    if (!kernel_console_step(&owner->Transcript, 10)) return finish(owner, 0);
    for (uint32_t index = 0; index < KERNEL_CONSOLE_TEXT_LIMIT; ++index) owner->Line[index] = 0;
    owner->Length = 0;
    return finish(owner, compose(owner));
}

// ====== kernel_console_service_render_begin ======

int kernel_console_service_render_begin(KERNEL_CONSOLE_SERVICE *owner)
{
    if (!enter(owner)) return 0;
    owner->Busy = 2;
    return 1;
}

// ====== kernel_console_service_render_end ======

int kernel_console_service_render_end(KERNEL_CONSOLE_SERVICE *owner, int success)
{
    if (!valid(owner) || owner->Busy != 2 || (success != 0 && success != 1)) return 0;
    return finish(owner, success);
}

// ====== kernel_console_service_pending ======

int kernel_console_service_pending(const KERNEL_CONSOLE_SERVICE *owner)
{
    uint32_t index, scalar;
    if (!valid(owner) || owner->Busy || owner->Failed || owner->Closed) return -1;
    return kernel_console_peek(owner->View, 0, &index, &scalar);
}

// ====== kernel_console_service_close ======

int kernel_console_service_close(KERNEL_CONSOLE_SERVICE *owner)
{
    if (!valid(owner) || owner->Busy || owner->Failed || owner->Closed) return 0;
    for (uint32_t index = 0; index < KERNEL_CONSOLE_TEXT_LIMIT; ++index) owner->Line[index] = 0;
    owner->Length = 0;
    owner->Closed = 1;
    return 1;
}
