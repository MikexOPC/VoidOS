#include "../../console.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static struct {
    uint64_t  Before;
    KERNEL_VIDEO_CONSOLE  Console;
    uint64_t  After;
} fixture;
static KERNEL_VIDEO_CONSOLE snapshot;
static unsigned int checks;
void kernel_console_render_test(void);
void kernel_console_measure_test(void);
#define CHECK(value) do { ++checks; if (!(value)) { \
    fprintf(stderr, "FAIL %u at line %d\n", checks, __LINE__); exit(1); } } while (0)

// ====== drain ======

static uint32_t drain(void)
{
    uint32_t start = 0, index, scalar, count = 0;
    int result;
    while ((result = kernel_console_peek(&fixture.Console, start, &index, &scalar)) == 1) {
        CHECK(index >= start);
        CHECK(kernel_console_ack(&fixture.Console, index, scalar));
        CHECK(!kernel_console_ack(&fixture.Console, index, scalar));
        start = index + 1;
        ++count;
    }
    CHECK(result == 0);
    return count;
}

// ====== invalid_and_ownership ======

static void invalid_and_ownership(void)
{
    uint32_t index = 123, scalar = 456;
    fixture.Before = 0xabcdef1234567890ULL;
    fixture.After = fixture.Before;
    CHECK(!kernel_console_init(NULL, 1, 1));
    CHECK(kernel_console_init(&fixture.Console, 3, 2));
    CHECK(drain() == 6);
    snapshot = fixture.Console;
    CHECK(!kernel_console_init(&fixture.Console, 241, 1));
    CHECK(!kernel_console_init(&fixture.Console, 1, 0));
    CHECK(!kernel_console_step(&fixture.Console, 0xd800));
    CHECK(!kernel_console_step(&fixture.Console, 9));
    CHECK(!kernel_console_step(&fixture.Console, 0x110000));
    CHECK(memcmp(&snapshot, &fixture.Console, sizeof(snapshot)) == 0);
    CHECK(!kernel_console_step(&snapshot, 65));
    CHECK(!kernel_console_clear(&snapshot));
    CHECK(kernel_console_peek(&snapshot, 0, &index, &scalar) == -1);
    CHECK(!kernel_console_ack(&snapshot, 0, 32));
    CHECK(kernel_console_peek(&fixture.Console, 7, &index, &scalar) == -1);
    CHECK(index == 123 && scalar == 456);
    CHECK(kernel_console_peek(&fixture.Console, 0, NULL, &scalar) == -1);
    CHECK(!kernel_console_ack(&fixture.Console, UINT32_MAX, 32));
    fixture.Console.Columns = UINT32_MAX;
    CHECK(!kernel_console_step(&fixture.Console, 65));
    CHECK(kernel_console_peek(&fixture.Console, 0, &index, &scalar) == -1);
    fixture.Console = snapshot;
    fixture.Console.Self = (uintptr_t)&fixture.Console;
    CHECK(fixture.Before == 0xabcdef1234567890ULL && fixture.After == fixture.Before);
}

// ====== editing_and_scroll ======

static void editing_and_scroll(void)
{
    uint32_t index, scalar;
    CHECK(kernel_console_init(&fixture.Console, 3, 2));
    CHECK(drain() == 6);
    CHECK(kernel_console_step(&fixture.Console, 8));
    CHECK(drain() == 0);
    CHECK(kernel_console_step(&fixture.Console, 'a'));
    CHECK(kernel_console_step(&fixture.Console, 0xe8));
    CHECK(kernel_console_step(&fixture.Console, 0x20ac));
    CHECK(fixture.Console.X == 3 && fixture.Console.Y == 0);
    CHECK(kernel_console_peek(&fixture.Console, 0, &index, &scalar) == 1 && scalar == 'a');
    CHECK(kernel_console_step(&fixture.Console, 13));
    CHECK(kernel_console_step(&fixture.Console, 'b'));
    CHECK(!kernel_console_ack(&fixture.Console, index, scalar));
    CHECK(drain() == 3);
    CHECK(kernel_console_step(&fixture.Console, 10));
    CHECK(kernel_console_step(&fixture.Console, 8));
    CHECK(fixture.Console.X == 2 && fixture.Console.Y == 0 && fixture.Console.Cells[2] == 32);
    CHECK(kernel_console_step(&fixture.Console, 10));
    CHECK(kernel_console_step(&fixture.Console, '1'));
    CHECK(kernel_console_step(&fixture.Console, '2'));
    CHECK(kernel_console_step(&fixture.Console, '3'));
    CHECK(kernel_console_step(&fixture.Console, '4'));
    CHECK(fixture.Console.X == 1 && fixture.Console.Y == 1);
    CHECK(fixture.Console.Cells[0] == '1' && fixture.Console.Cells[1] == '2' &&
        fixture.Console.Cells[2] == '3' && fixture.Console.Cells[3] == '4' &&
        fixture.Console.Cells[4] == 32 && fixture.Console.Cells[5] == 32);
    CHECK(drain() == 6);
    CHECK(kernel_console_step(&fixture.Console, 10));
    CHECK(fixture.Console.Cells[0] == '4' && fixture.Console.Cells[3] == 32);
    CHECK(kernel_console_clear(&fixture.Console));
    CHECK(fixture.Console.X == 0 && fixture.Console.Y == 0);
    CHECK(drain() == 6);
    CHECK(kernel_console_init(&fixture.Console, 1, 1));
    CHECK(kernel_console_step(&fixture.Console, 'A'));
    CHECK(kernel_console_step(&fixture.Console, 'B'));
    CHECK(fixture.Console.Cells[0] == 'B');
    CHECK(kernel_console_step(&fixture.Console, 10));
    CHECK(fixture.Console.Cells[0] == 32 && fixture.Console.X == 0);
    CHECK(drain() == 1);
}

// ====== maximum_and_stress ======

static void maximum_and_stress(void)
{
    uint32_t index, cycle;
    CHECK(kernel_console_init(&fixture.Console, KERNEL_CONSOLE_COLUMNS, KERNEL_CONSOLE_ROWS));
    CHECK(drain() == KERNEL_CONSOLE_CELLS);
    for (index = 0; index < KERNEL_CONSOLE_CELLS; ++index) {
        CHECK(kernel_console_step(&fixture.Console, 32 + index % 95));
        CHECK(fixture.Console.Cells[index] == 32 + index % 95);
    }
    CHECK(fixture.Console.X == KERNEL_CONSOLE_COLUMNS && fixture.Console.Y == KERNEL_CONSOLE_ROWS - 1);
    CHECK(kernel_console_step(&fixture.Console, 0x20ac));
    for (index = 0; index < KERNEL_CONSOLE_CELLS - KERNEL_CONSOLE_COLUMNS; ++index)
        CHECK(fixture.Console.Cells[index] == 32 + (index + KERNEL_CONSOLE_COLUMNS) % 95);
    CHECK(drain() == KERNEL_CONSOLE_CELLS);
    for (cycle = 0; cycle < 10000; ++cycle) {
        CHECK(kernel_console_init(&fixture.Console, 2, 2));
        CHECK(kernel_console_step(&fixture.Console, 0xe8));
        CHECK(kernel_console_step(&fixture.Console, 8));
        CHECK(fixture.Console.Cells[0] == 32);
        CHECK(kernel_console_step(&fixture.Console, 0x20ac));
        CHECK(kernel_console_step(&fixture.Console, 10));
        CHECK(kernel_console_step(&fixture.Console, 'X'));
        CHECK(kernel_console_step(&fixture.Console, 10));
        CHECK(fixture.Console.Cells[0] == 'X' && fixture.Console.Cells[2] == 32);
        CHECK(drain() == 4);
    }
    CHECK(fixture.Before == 0xabcdef1234567890ULL && fixture.After == fixture.Before);
}

// ====== main ======

int main(void)
{
    invalid_and_ownership();
    editing_and_scroll();
    maximum_and_stress();
    kernel_console_render_test();
    kernel_console_measure_test();
    printf("PASS console grid: %u checks; size %zu; no allocations\n", checks, sizeof(fixture.Console));
    return 0;
}
