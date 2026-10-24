#include "lib/geometry/damage.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned long checks;
#define CHECK(value) do { ++checks; if (!(value)) { printf("FAIL geometry line %d\n", __LINE__); exit(1); } } while (0)

// ====== covers_private ======

static int covers_private(const OS_RECT *rect, uint32_t x, uint32_t y)
{
    return !os_rect_empty(rect) && x >= rect->X && y >= rect->Y && x - rect->X < rect->Width && y - rect->Y < rect->Height;
}

// ====== next_private ======

// Deterministic xorshift, so every failure is reproducible.
static uint32_t state = 0x2545f491U;

static uint32_t next_private(void)
{
    state ^= state << 13; state ^= state >> 17; state ^= state << 5;
    return state;
}

// ====== rect_oracle ======

// Every rectangle with corners in 0..5 against a pixel oracle on a 6x6 grid.
static void rect_oracle(void)
{
    enum { N = 6 };
    OS_RECT all[N * N * (N + 1) * (N + 1)];
    uint32_t count = 0;

    for (uint32_t x = 0; x < N; ++x) for (uint32_t y = 0; y < N; ++y)
        for (uint32_t w = 0; w <= N - x; ++w) for (uint32_t h = 0; h <= N - y; ++h) all[count++] = (OS_RECT){x, y, w, h};

    for (uint32_t first = 0; first < count; ++first) for (uint32_t second = 0; second < count; ++second) {
        const OS_RECT *a = &all[first], *b = &all[second];
        OS_RECT joined, common;

        CHECK(os_rect_union(a, b, &joined) && os_rect_intersect(a, b, &common));

        for (uint32_t x = 0; x < N; ++x) for (uint32_t y = 0; y < N; ++y) {
            int in_a = covers_private(a, x, y), in_b = covers_private(b, x, y);

            CHECK(covers_private(&common, x, y) == (in_a && in_b));
            if (in_a || in_b) CHECK(covers_private(&joined, x, y));
        }

        CHECK(os_rect_contains(&joined, a) && os_rect_contains(&joined, b));
        CHECK(os_rect_contains(a, &common) && os_rect_contains(b, &common));
        if (os_rect_empty(&common)) CHECK(!common.X && !common.Y && !common.Width && !common.Height);
        if (os_rect_empty(a) && os_rect_empty(b)) CHECK(os_rect_empty(&joined) && !joined.X && !joined.Y);
        // The union is the smallest box: its edges are the extreme edges of the non-empty operands.
        if (!os_rect_empty(a) && !os_rect_empty(b)) {
            uint32_t left = a->X < b->X ? a->X : b->X, top = a->Y < b->Y ? a->Y : b->Y;
            uint32_t right = a->X + a->Width > b->X + b->Width ? a->X + a->Width : b->X + b->Width;
            uint32_t bottom = a->Y + a->Height > b->Y + b->Height ? a->Y + a->Height : b->Y + b->Height;

            CHECK(joined.X == left && joined.Y == top && joined.Width == right - left && joined.Height == bottom - top);
        }
        else if (!os_rect_empty(a)) CHECK(!memcmp(&joined, a, sizeof(joined)));
        else if (!os_rect_empty(b)) CHECK(!memcmp(&joined, b, sizeof(joined)));
    }
}

// ====== rect_bounds ======

static void rect_bounds(void)
{
    OS_RECT out, max = {UINT32_MAX - 4, 0, 4, 1}, over = {UINT32_MAX - 4, 0, 5, 1};

    CHECK(os_rect_valid(&max) && !os_rect_valid(&over) && !os_rect_valid(0));
    CHECK(!os_rect_union(&over, &max, &out) && !os_rect_intersect(&max, &over, &out) && !os_rect_union(&max, &max, 0));
    CHECK(os_rect_area(&over) == 0 && os_rect_area(&max) == 4 && !os_rect_contains(&over, &max));
    CHECK(os_rect_area(&(OS_RECT){0, 0, UINT32_MAX, UINT32_MAX}) == (uint64_t)UINT32_MAX * UINT32_MAX);

    CHECK(os_rect_clip(&(OS_RECT){1270, 790, 40, 40}, 1280, 800, &out) && out.X == 1270 && out.Width == 10 && out.Height == 10);
    CHECK(os_rect_clip(&(OS_RECT){1280, 0, 4, 4}, 1280, 800, &out) && os_rect_empty(&out));
    CHECK(os_rect_clip(&(OS_RECT){0, 0, 1024, 768}, 1280, 800, &out) && out.Width == 1024 && out.Height == 768);
}

// ====== damage_cases ======

// Random adds on a 64x64 surface: every added pixel stays covered, Count never exceeds Limit,
// Limit 1 equals the envelope of all adds, and covered rectangles add nothing.
static void damage_cases(void)
{
    OS_DAMAGE damage, envelope;
    OS_RECT bounds, expected;

    CHECK(!os_damage_init(&damage, 0) && !os_damage_init(&damage, OS_DAMAGE_RECTS + 1) && !os_damage_init(0, 1));

    for (uint32_t limit = 1; limit <= OS_DAMAGE_RECTS; ++limit)
        for (uint32_t round = 0; round < 2000; ++round) {
            static unsigned char added[64][64];
            uint32_t adds = 1 + next_private() % 12;

            memset(added, 0, sizeof(added));
            CHECK(os_damage_init(&damage, limit) && os_damage_init(&envelope, 1) && os_damage_empty(&damage));
            expected = (OS_RECT){0};

            for (uint32_t add = 0; add < adds; ++add) {
                uint32_t x = next_private() % 64, y = next_private() % 64;
                OS_RECT rect = {x, y, next_private() % (65 - x), next_private() % (65 - y)};

                CHECK(os_damage_add(&damage, &rect) && os_damage_add(&envelope, &rect));
                CHECK(os_rect_union(&expected, &rect, &expected));
                CHECK(damage.Count <= limit && envelope.Count <= 1);

                for (uint32_t row = rect.Y; row < rect.Y + rect.Height; ++row)
                    for (uint32_t column = rect.X; column < rect.X + rect.Width; ++column) added[row][column] = 1;
            }

            for (uint32_t row = 0; row < 64; ++row) for (uint32_t column = 0; column < 64; ++column) {
                int covered = 0;

                for (uint32_t index = 0; index < damage.Count; ++index) covered |= covers_private(&damage.Rects[index], column, row);

                if (added[row][column]) CHECK(covered);
            }

            CHECK(os_damage_bounds(&envelope, &bounds) && !memcmp(&bounds, &expected, sizeof(bounds)));
            CHECK(os_damage_bounds(&damage, &bounds) && !memcmp(&bounds, &expected, sizeof(bounds)));
            for (uint32_t index = damage.Count; index < OS_DAMAGE_RECTS; ++index) CHECK(os_rect_empty(&damage.Rects[index]));
        }

    // Covered and covering rectangles; empty and invalid input.
    CHECK(os_damage_init(&damage, 4));
    CHECK(os_damage_add(&damage, &(OS_RECT){10, 10, 5, 5}) && os_damage_add(&damage, &(OS_RECT){11, 11, 2, 2}) && damage.Count == 1);
    CHECK(os_damage_add(&damage, &(OS_RECT){40, 40, 2, 2}) && damage.Count == 2);
    CHECK(os_damage_add(&damage, &(OS_RECT){0, 0, 20, 20}) && damage.Count == 2);
    CHECK(os_damage_add(&damage, &(OS_RECT){5, 5, 0, 9}) && damage.Count == 2);
    CHECK(!os_damage_add(&damage, &(OS_RECT){UINT32_MAX, 0, 2, 1}) && damage.Count == 2);
    // Far apart rectangles stay separate below the limit; the cheapest pair merges past it.
    CHECK(os_damage_init(&damage, 2));
    CHECK(os_damage_add(&damage, &(OS_RECT){0, 0, 4, 4}) && os_damage_add(&damage, &(OS_RECT){1000, 1000, 4, 4}));
    CHECK(os_damage_add(&damage, &(OS_RECT){6, 0, 4, 4}) && damage.Count == 2);
    CHECK(damage.Rects[0].X == 0 && damage.Rects[0].Width == 10 && damage.Rects[1].X == 1000);
    os_damage_clear(&damage);
    CHECK(os_damage_empty(&damage) && os_damage_bounds(&damage, &bounds) && os_rect_empty(&bounds));
}

// ====== main ======

int main(void)
{
    rect_oracle();
    rect_bounds();
    damage_cases();
    printf("PASS geometry: %lu checks; 6x6 pixel oracle, bounds, 8000 random damage rounds\n", checks);
    return 0;
}
