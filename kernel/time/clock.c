#include "clock.h"

// ====== kernel_clock_valid ======

int kernel_clock_valid(const KERNEL_CLOCK *clock)
{
    return clock && clock->Read && (clock->Mask == 0x00ffffffU || clock->Mask == 0xffffffffU) && clock->Hz;
}

// ====== kernel_clock_now ======

int kernel_clock_now(const KERNEL_CLOCK *clock, uint32_t *now)
{
    uint32_t value = 0;

    if (!kernel_clock_valid(clock) || !now || !clock->Read(clock->Context, &value) || value > clock->Mask) return 0;

    *now = value;
    return 1;
}

// ====== kernel_clock_delta ======

uint32_t kernel_clock_delta(const KERNEL_CLOCK *clock, uint32_t start, uint32_t end)
{
    return kernel_clock_valid(clock) ? kernel_counter_delta(clock->Mask, start, end) : 0;
}

// ====== kernel_counter_delta ======

uint32_t kernel_counter_delta(uint32_t mask, uint32_t start, uint32_t end)
{
    return mask == 0x00ffffffU || mask == 0xffffffffU ? (end - start) & mask : 0;
}

// ====== kernel_clock_elapsed ======

uint32_t kernel_clock_elapsed(const KERNEL_CLOCK *clock, uint32_t start)
{
    uint32_t now;

    return kernel_clock_now(clock, &now) ? kernel_clock_delta(clock, start, now) : UINT32_MAX;
}

// ====== ticks_private ======

// value / unit seconds in ticks, rounded up, valid only below half a counter period.
static uint32_t ticks_private(const KERNEL_CLOCK *clock, uint32_t value, uint64_t unit)
{
    if (!kernel_clock_valid(clock)) return UINT32_MAX;

    uint64_t ticks = ((uint64_t)value * clock->Hz + unit - 1) / unit;

    return ticks < clock->Mask / 2 ? (uint32_t)ticks : UINT32_MAX;
}

// ====== kernel_clock_ms ======

uint32_t kernel_clock_ms(const KERNEL_CLOCK *clock, uint32_t milliseconds)
{
    return ticks_private(clock, milliseconds, 1000);
}

// ====== kernel_clock_us ======

uint32_t kernel_clock_us(const KERNEL_CLOCK *clock, uint32_t microseconds)
{
    return ticks_private(clock, microseconds, 1000000);
}

// ====== kernel_deadline_start ======

int kernel_deadline_start(const KERNEL_CLOCK *clock, uint32_t ticks, KERNEL_DEADLINE *deadline)
{
    uint32_t now;

    if (!deadline || !kernel_clock_valid(clock) || ticks >= clock->Mask / 2 || !kernel_clock_now(clock, &now)) return 0;

    *deadline = (KERNEL_DEADLINE){now, ticks};
    return 1;
}

// ====== kernel_deadline_expired ======

int kernel_deadline_expired(const KERNEL_CLOCK *clock, const KERNEL_DEADLINE *deadline)
{
    if (!deadline) return 1;

    uint32_t elapsed = kernel_clock_elapsed(clock, deadline->Start);

    return elapsed == UINT32_MAX || elapsed >= deadline->Ticks;
}

// ====== kernel_stopwatch_start ======

int kernel_stopwatch_start(const KERNEL_CLOCK *clock, KERNEL_STOPWATCH *watch)
{
    uint32_t now;

    if (!watch || !kernel_clock_now(clock, &now)) return 0;

    *watch = (KERNEL_STOPWATCH){now, 0, 0};
    return 1;
}

// ====== kernel_stopwatch_sample ======

// A gap of half a period or more cannot be told apart from a wrap: it is added once, marked Lost.
int kernel_stopwatch_sample(const KERNEL_CLOCK *clock, KERNEL_STOPWATCH *watch)
{
    uint32_t now;

    if (!watch || !kernel_clock_now(clock, &now)) return 0;

    uint32_t delta = kernel_clock_delta(clock, watch->Last, now);

    if (delta >= clock->Mask / 2) watch->Lost = 1;

    watch->Total = watch->Total > UINT64_MAX - delta ? UINT64_MAX : watch->Total + delta;
    watch->Last = now;
    return 1;
}
