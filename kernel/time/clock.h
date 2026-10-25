#ifndef OS_KERNEL_TIME_CLOCK_H
#define OS_KERNEL_TIME_CLOCK_H

#include <stdint.h>

// G4: elapsed ticks, deadlines and long intervals over a free-running counter of Mask + 1 values
// (24 or 32 bit). The source is a parameter: today the ACPI PM timer, later a calibrated TSC or
// the HPET without changing the callers (C4).
//
// ACPI 6.5 4.8.3.3 Power Management Timer: 3.579545 MHz, 24 bit (32 with TMR_VAL_EXT in the FADT).
#define KERNEL_PM_TIMER_HZ  3579545U

typedef struct {
    int       (*Read)(void *context, uint32_t *value);
    void      *Context;
    uint32_t  Mask;       // 0x00ffffff or 0xffffffff
    uint32_t  Hz;
} KERNEL_CLOCK;

typedef struct {
    uint32_t  Start;
    uint32_t  Ticks;
} KERNEL_DEADLINE;

// Accumulates across counter wraps; Lost is set when two samples were more than half a period
// apart, because then the wrap count is ambiguous and the total can no longer be trusted.
typedef struct {
    uint32_t  Last;
    uint32_t  Lost;
    uint64_t  Total;
} KERNEL_STOPWATCH;

int       kernel_clock_valid(const KERNEL_CLOCK *clock);
int       kernel_clock_now(const KERNEL_CLOCK *clock, uint32_t *now);
uint32_t  kernel_clock_delta(const KERNEL_CLOCK *clock, uint32_t start, uint32_t end);
// For callers that already hold raw samples: ticks from start to end of a 24- or 32-bit counter,
// 0 for any other mask.
uint32_t  kernel_counter_delta(uint32_t mask, uint32_t start, uint32_t end);
// Ticks since start, or UINT32_MAX when the clock cannot be read.
uint32_t  kernel_clock_elapsed(const KERNEL_CLOCK *clock, uint32_t start);
// Ticks for a duration, rounded up; UINT32_MAX when it is not below half a counter period.
uint32_t  kernel_clock_ms(const KERNEL_CLOCK *clock, uint32_t milliseconds);
uint32_t  kernel_clock_us(const KERNEL_CLOCK *clock, uint32_t microseconds);

int       kernel_deadline_start(const KERNEL_CLOCK *clock, uint32_t ticks, KERNEL_DEADLINE *deadline);
// 1 expired (or the clock failed: callers fail closed), 0 still running.
int       kernel_deadline_expired(const KERNEL_CLOCK *clock, const KERNEL_DEADLINE *deadline);

int       kernel_stopwatch_start(const KERNEL_CLOCK *clock, KERNEL_STOPWATCH *watch);
// Adds the ticks since the previous sample; 0 when the clock failed.
int       kernel_stopwatch_sample(const KERNEL_CLOCK *clock, KERNEL_STOPWATCH *watch);

#endif
