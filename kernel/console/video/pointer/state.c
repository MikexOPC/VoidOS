#include "state.h"

// ====== separate ======

static int separate(const KERNEL_CONSOLE_POINTER *owner, const KERNEL_VIDEO_CONSOLE *view)
{
    uintptr_t first = (uintptr_t)owner, second = (uintptr_t)view;
    return first <= UINTPTR_MAX - sizeof(*owner) && second <= UINTPTR_MAX - sizeof(*view) &&
        (first + sizeof(*owner) <= second || second + sizeof(*view) <= first);
}

// ====== kernel_pointer_valid ======

int kernel_pointer_valid(const KERNEL_CONSOLE_POINTER *owner)
{
    uint32_t index, scalar;
    return owner && (uintptr_t)owner % _Alignof(KERNEL_CONSOLE_POINTER) == 0 &&
        owner->Self == (uintptr_t)owner && owner->View && separate(owner, owner->View) &&
        !owner->Closed && owner->Image.Visible <= 1 && owner->Buttons <= 7 &&
        kernel_console_peek(owner->View, 0, &index, &scalar) >= 0 &&
        kernel_pointer_plan(owner->View->Columns * 8, owner->View->Rows * 16,
            owner->Image.X, owner->Image.Y, 0, 0) != UINT64_MAX;
}

// ====== invalidate ======

static void invalidate(KERNEL_VIDEO_CONSOLE *view, const KERNEL_POINTER_IMAGE *image)
{
    for (uint32_t row = image->Y / 16; row <= (image->Y + 23) / 16; ++row)
        for (uint32_t column = image->X / 8; column <= (image->X + 23) / 8; ++column) {
            uint32_t index = row * view->Columns + column;
            view->Dirty[index / 8] |= (uint8_t)(1U << (index % 8));
        }
}

// ====== kernel_pointer_init ======

int kernel_pointer_init(KERNEL_CONSOLE_POINTER *owner, KERNEL_VIDEO_CONSOLE *view)
{
    uint32_t index, scalar;
    uint64_t plan;
    if (!owner || (uintptr_t)owner % _Alignof(KERNEL_CONSOLE_POINTER) || owner->Self ||
        !view || !separate(owner, view) || kernel_console_peek(view, 0, &index, &scalar) < 0) return 0;
    plan = kernel_pointer_plan(view->Columns * 8, view->Rows * 16, 0, 0,
        0, 0);
    if (plan == UINT64_MAX) return 0;
    owner->Self = (uintptr_t)owner; owner->View = view;
    owner->Image.X = (view->Columns * 8 - 24) / 2;
    owner->Image.Y = (view->Rows * 16 - 24) / 2;
    owner->Image.Visible = 1; owner->Buttons = owner->Closed = 0; owner->Moves = 0;
    invalidate(view, &owner->Image);
    return 1;
}

// ====== kernel_pointer_move ======

static int move_private(KERNEL_CONSOLE_POINTER *owner, int32_t dx, int32_t dy, uint32_t buttons, int dirty)
{
    uint64_t plan;
    if (!kernel_pointer_valid(owner) || !owner->Image.Visible || buttons > 7 || owner->Moves == UINT64_MAX) return 0;
    plan = kernel_pointer_plan(owner->View->Columns * 8, owner->View->Rows * 16,
        owner->Image.X, owner->Image.Y, dx, dy);
    if (plan == UINT64_MAX) return 0;
    if (plan != ((uint64_t)owner->Image.Y << 32 | owner->Image.X)) {
        if (dirty) invalidate(owner->View, &owner->Image);
        owner->Image.X = (uint32_t)plan; owner->Image.Y = (uint32_t)(plan >> 32);
        if (dirty) invalidate(owner->View, &owner->Image);
        ++owner->Moves;
    }
    owner->Buttons = buttons;
    return 1;
}

// ====== kernel_pointer_move ======

int kernel_pointer_move(KERNEL_CONSOLE_POINTER *owner, int32_t dx, int32_t dy, uint32_t buttons)
{
    return move_private(owner, dx, dy, buttons, 1);
}

// ====== kernel_pointer_request ======

int kernel_pointer_request(KERNEL_CONSOLE_POINTER *owner, int32_t dx, int32_t dy, uint32_t buttons)
{
    return move_private(owner, dx, dy, buttons, 0);
}

// ====== kernel_pointer_hide ======

int kernel_pointer_hide(KERNEL_CONSOLE_POINTER *owner)
{
    if (!kernel_pointer_valid(owner)) return 0;
    if (owner->Image.Visible) invalidate(owner->View, &owner->Image);
    owner->Image.Visible = owner->Buttons = 0;
    return 1;
}

// ====== kernel_pointer_close ======

int kernel_pointer_close(KERNEL_CONSOLE_POINTER *owner)
{
    uint32_t index, scalar;
    if (!kernel_pointer_valid(owner) || owner->Image.Visible || owner->Buttons ||
        kernel_console_peek(owner->View, 0, &index, &scalar) != 0) return 0;
    owner->Closed = 1;
    return 1;
}
