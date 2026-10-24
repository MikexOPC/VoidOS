#ifndef OS_LIB_GEOMETRY_RECT_H
#define OS_LIB_GEOMETRY_RECT_H

#include <stdint.h>

// G1: pixel rectangles shared by the console, the present path and the future GUI. Width or
// Height 0 is the empty rectangle; X + Width and Y + Height never exceed 2^32 - 1 in a valid one.
typedef struct {
    uint32_t  X;
    uint32_t  Y;
    uint32_t  Width;
    uint32_t  Height;
} OS_RECT;

int       os_rect_valid(const OS_RECT *rect);
int       os_rect_empty(const OS_RECT *rect);
uint64_t  os_rect_area(const OS_RECT *rect);
int       os_rect_contains(const OS_RECT *outer, const OS_RECT *inner);
int       os_rect_union(const OS_RECT *first, const OS_RECT *second, OS_RECT *output);
int       os_rect_intersect(const OS_RECT *first, const OS_RECT *second, OS_RECT *output);
int       os_rect_clip(const OS_RECT *rect, uint32_t width, uint32_t height, OS_RECT *output);

#endif
