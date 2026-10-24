#include "rect.h"

// ====== os_rect_valid ======

int os_rect_valid(const OS_RECT *rect)
{
    return rect &&
        (uint64_t)rect->X + rect->Width <= UINT32_MAX &&
        (uint64_t)rect->Y + rect->Height <= UINT32_MAX;
}

// ====== os_rect_empty ======

int os_rect_empty(const OS_RECT *rect)
{
    return !rect || !rect->Width || !rect->Height;
}

// ====== os_rect_area ======

uint64_t os_rect_area(const OS_RECT *rect)
{
    return os_rect_valid(rect) ? (uint64_t)rect->Width * rect->Height : 0;
}

// ====== os_rect_contains ======

// An empty inner rectangle is contained in any valid outer one.
int os_rect_contains(const OS_RECT *outer, const OS_RECT *inner)
{
    if (!os_rect_valid(outer) || !os_rect_valid(inner)) return 0;

    if (os_rect_empty(inner)) return 1;

    return !os_rect_empty(outer) &&
        inner->X >= outer->X && inner->Y >= outer->Y &&
        inner->X + inner->Width <= outer->X + outer->Width &&
        inner->Y + inner->Height <= outer->Y + outer->Height;
}

// ====== os_rect_union ======

// Smallest rectangle covering both; an empty operand does not widen the result.
int os_rect_union(const OS_RECT *first, const OS_RECT *second, OS_RECT *output)
{
    uint32_t left, top, right, bottom;

    if (!os_rect_valid(first) || !os_rect_valid(second) || !output) return 0;

    if (os_rect_empty(first)) { *output = os_rect_empty(second) ? (OS_RECT){0} : *second; return 1; }

    if (os_rect_empty(second)) { *output = *first; return 1; }

    left = first->X < second->X ? first->X : second->X;
    top = first->Y < second->Y ? first->Y : second->Y;
    right = first->X + first->Width > second->X + second->Width ? first->X + first->Width : second->X + second->Width;
    bottom = first->Y + first->Height > second->Y + second->Height ? first->Y + first->Height : second->Y + second->Height;

    *output = (OS_RECT){left, top, right - left, bottom - top};
    return 1;
}

// ====== os_rect_intersect ======

// The common part, empty ({0}) when the rectangles do not overlap.
int os_rect_intersect(const OS_RECT *first, const OS_RECT *second, OS_RECT *output)
{
    uint32_t left, top, right, bottom;

    if (!os_rect_valid(first) || !os_rect_valid(second) || !output) return 0;

    left = first->X > second->X ? first->X : second->X;
    top = first->Y > second->Y ? first->Y : second->Y;
    right = first->X + first->Width < second->X + second->Width ? first->X + first->Width : second->X + second->Width;
    bottom = first->Y + first->Height < second->Y + second->Height ? first->Y + first->Height : second->Y + second->Height;

    *output = left < right && top < bottom ? (OS_RECT){left, top, right - left, bottom - top} : (OS_RECT){0};
    return 1;
}

// ====== os_rect_clip ======

// The part of rect inside a width x height surface anchored at 0,0.
int os_rect_clip(const OS_RECT *rect, uint32_t width, uint32_t height, OS_RECT *output)
{
    OS_RECT bounds = {0, 0, width, height};

    return os_rect_intersect(rect, &bounds, output);
}
