#ifndef OS_LIB_GEOMETRY_DAMAGE_H
#define OS_LIB_GEOMETRY_DAMAGE_H

#include "rect.h"

// G1: up to OS_DAMAGE_RECTS dirty rectangles. Past Limit the pair whose union wastes the least
// area is merged; Limit 1 is the single envelope used by the V2c present today. The merge policy
// stays here so a multi-rectangle present (FW-011) does not change the callers.
#define OS_DAMAGE_RECTS 4U

typedef struct {
    uint32_t  Count;
    uint32_t  Limit;
    OS_RECT   Rects[OS_DAMAGE_RECTS];
} OS_DAMAGE;

int  os_damage_init(OS_DAMAGE *damage, uint32_t limit);
int  os_damage_add(OS_DAMAGE *damage, const OS_RECT *rect);
int  os_damage_bounds(const OS_DAMAGE *damage, OS_RECT *output);
int  os_damage_empty(const OS_DAMAGE *damage);
void os_damage_clear(OS_DAMAGE *damage);

#endif
