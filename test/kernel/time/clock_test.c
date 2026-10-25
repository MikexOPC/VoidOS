#include "kernel/time/clock.h"
#include <stdio.h>
#include <stdlib.h>

static unsigned long checks;
#define CHECK(value) do { ++checks; if (!(value)) { printf("FAIL clock line %d\n", __LINE__); exit(1); } } while (0)

// ====== mock ======

typedef struct { uint32_t Value, Fail, Garbage; } MOCK;

static int read_private(void *context, uint32_t *value)
{
    MOCK *mock = context;

    if (mock->Fail) return 0;

    *value = mock->Garbage ? 0xffffffffU : mock->Value;
    return 1;
}

// ====== width_cases ======

// Elapsed across the wrap, deadlines at the exact boundary, conversions, stopwatch accumulation.
static void width_cases(uint32_t mask)
{
    MOCK mock = {0};
    KERNEL_CLOCK clock = {read_private, &mock, mask, KERNEL_PM_TIMER_HZ};
    KERNEL_DEADLINE deadline;
    KERNEL_STOPWATCH watch;
    uint32_t now;

    CHECK(kernel_clock_valid(&clock));

    // Elapsed across the wrap of the counter.
    mock.Value = mask - 9;
    CHECK(kernel_clock_now(&clock, &now) && now == mask - 9);
    mock.Value = 5;
    CHECK(kernel_clock_elapsed(&clock, mask - 9) == 15 && kernel_clock_delta(&clock, mask - 9, 5) == 15);

    // A deadline expires exactly at its tick count, also across the wrap.
    mock.Value = mask - 100;
    CHECK(kernel_deadline_start(&clock, 357955, &deadline) && deadline.Start == mask - 100);
    mock.Value = (mask - 100 + 357954) & mask;
    CHECK(!kernel_deadline_expired(&clock, &deadline));
    mock.Value = (mask - 100 + 357955) & mask;
    CHECK(kernel_deadline_expired(&clock, &deadline));
    CHECK(!kernel_deadline_start(&clock, mask / 2, &deadline) && kernel_deadline_expired(&clock, 0));

    // Conversions round up; values not below half a period are refused.
    CHECK(kernel_clock_ms(&clock, 100) == 357955 && kernel_clock_ms(&clock, 1000) == 3579545);
    CHECK(kernel_clock_us(&clock, 1) == 4 && kernel_clock_us(&clock, 0) == 0 && kernel_clock_ms(&clock, 0) == 0);
    uint32_t half_ms = (uint32_t)((uint64_t)(mask / 2) * 1000 / KERNEL_PM_TIMER_HZ);
    CHECK(kernel_clock_ms(&clock, half_ms - 1) != UINT32_MAX && kernel_clock_ms(&clock, half_ms + 1) == UINT32_MAX);
    CHECK(kernel_clock_ms(&clock, UINT32_MAX) == UINT32_MAX);

    // Stopwatch: 1000 samples a quarter period apart add up over several wraps.
    mock.Value = 7;
    CHECK(kernel_stopwatch_start(&clock, &watch));
    for (uint32_t sample = 0; sample < 1000; ++sample) {
        mock.Value = (mock.Value + mask / 4) & mask;
        CHECK(kernel_stopwatch_sample(&clock, &watch));
    }
    CHECK(watch.Total == 1000ULL * (mask / 4) && !watch.Lost);

    // A sample after more than half a period cannot tell a wrap from a gap: marked Lost.
    mock.Value = (mock.Value + mask / 2 + 1) & mask;
    CHECK(kernel_stopwatch_sample(&clock, &watch) && watch.Lost);

    // Clock failures: elapsed reports UINT32_MAX, deadlines count as expired (fail closed).
    CHECK(kernel_deadline_start(&clock, 10, &deadline));
    mock.Fail = 1;
    CHECK(kernel_clock_elapsed(&clock, 0) == UINT32_MAX && kernel_deadline_expired(&clock, &deadline));
    CHECK(!kernel_clock_now(&clock, &now) && !kernel_stopwatch_sample(&clock, &watch) && !kernel_stopwatch_start(&clock, &watch));
    mock.Fail = 0;
    if (mask != 0xffffffffU) {
        mock.Garbage = 1;  // a value outside a 24-bit counter is refused
        CHECK(!kernel_clock_now(&clock, &now) && kernel_deadline_expired(&clock, &deadline));
        mock.Garbage = 0;
    }
}

// ====== invalid_cases ======

static void invalid_cases(void)
{
    MOCK mock = {0};
    KERNEL_CLOCK bad = {read_private, &mock, 0x0fffffff, KERNEL_PM_TIMER_HZ};
    uint32_t now;

    CHECK(!kernel_clock_valid(&bad) && !kernel_clock_valid(0) && !kernel_clock_now(&bad, &now));
    bad.Mask = 0xffffff; bad.Hz = 0;
    CHECK(!kernel_clock_valid(&bad) && kernel_clock_ms(&bad, 1) == UINT32_MAX && kernel_clock_delta(&bad, 0, 5) == 0);
    bad.Hz = KERNEL_PM_TIMER_HZ; bad.Read = 0;
    CHECK(!kernel_clock_valid(&bad) && kernel_clock_elapsed(&bad, 0) == UINT32_MAX);
    CHECK(kernel_counter_delta(0xffffff, 0xfffffe, 3) == 5 && kernel_counter_delta(0xffffffff, 0xfffffffe, 3) == 5);
    CHECK(kernel_counter_delta(0x0fffffff, 0, 5) == 0 && kernel_counter_delta(0, 0, 5) == 0);
}

// ====== main ======

int main(void)
{
    width_cases(0x00ffffffU);
    width_cases(0xffffffffU);
    invalid_cases();
    printf("PASS clock: %lu checks; 24/32-bit wraps, deadlines, conversions, stopwatch, failures\n", checks);
    return 0;
}
