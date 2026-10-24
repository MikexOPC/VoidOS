#include "damage.h"

// ====== valid_private ======

static int valid_private(const OS_DAMAGE *damage)
{
    return damage && damage->Limit >= 1 && damage->Limit <= OS_DAMAGE_RECTS && damage->Count <= damage->Limit;
}

// ====== os_damage_init ======

int os_damage_init(OS_DAMAGE *damage, uint32_t limit)
{
    if (!damage || !limit || limit > OS_DAMAGE_RECTS) return 0;

    *damage = (OS_DAMAGE){0};
    damage->Limit = limit;
    return 1;
}

// ====== waste_private ======

// Area the union covers beyond the two rectangles; their overlap is counted once.
static uint64_t waste_private(const OS_RECT *first, const OS_RECT *second)
{
    OS_RECT joined, common;

    (void)os_rect_union(first, second, &joined);
    (void)os_rect_intersect(first, second, &common);
    return os_rect_area(&joined) + os_rect_area(&common) - os_rect_area(first) - os_rect_area(second);
}

// ====== merge_private ======

// Merges the pair with the least waste into the first of the two and closes the gap.
static void merge_private(OS_RECT *rects, uint32_t *count)
{
    uint32_t best_first = 0, best_second = 1;
    uint64_t best = UINT64_MAX;

    for (uint32_t first = 0; first < *count; ++first)
        for (uint32_t second = first + 1; second < *count; ++second) {
            uint64_t waste = waste_private(&rects[first], &rects[second]);

            if (waste < best) { best = waste; best_first = first; best_second = second; }
        }

    (void)os_rect_union(&rects[best_first], &rects[best_second], &rects[best_first]);

    for (uint32_t index = best_second; index + 1 < *count; ++index) rects[index] = rects[index + 1];

    --*count;
}

// ====== os_damage_add ======

// Empty rectangles are ignored, a covered one adds nothing, rectangles it covers are dropped;
// past Limit the cheapest pairs are merged.
int os_damage_add(OS_DAMAGE *damage, const OS_RECT *rect)
{
    OS_RECT pending[OS_DAMAGE_RECTS + 1];
    uint32_t count = 0;

    if (!valid_private(damage) || !os_rect_valid(rect)) return 0;

    if (os_rect_empty(rect)) return 1;

    for (uint32_t index = 0; index < damage->Count; ++index)
        if (os_rect_contains(&damage->Rects[index], rect)) return 1;

    for (uint32_t index = 0; index < damage->Count; ++index)
        if (!os_rect_contains(rect, &damage->Rects[index])) pending[count++] = damage->Rects[index];

    pending[count++] = *rect;

    while (count > damage->Limit) merge_private(pending, &count);

    for (uint32_t index = 0; index < OS_DAMAGE_RECTS; ++index) damage->Rects[index] = index < count ? pending[index] : (OS_RECT){0};

    damage->Count = count;
    return 1;
}

// ====== os_damage_bounds ======

int os_damage_bounds(const OS_DAMAGE *damage, OS_RECT *output)
{
    OS_RECT bounds = {0};

    if (!valid_private(damage) || !output) return 0;

    for (uint32_t index = 0; index < damage->Count; ++index) (void)os_rect_union(&bounds, &damage->Rects[index], &bounds);

    *output = bounds;
    return 1;
}

// ====== os_damage_empty ======

int os_damage_empty(const OS_DAMAGE *damage)
{
    return !valid_private(damage) || !damage->Count;
}

// ====== os_damage_clear ======

void os_damage_clear(OS_DAMAGE *damage)
{
    if (!valid_private(damage)) return;

    for (uint32_t index = 0; index < OS_DAMAGE_RECTS; ++index) damage->Rects[index] = (OS_RECT){0};

    damage->Count = 0;
}
