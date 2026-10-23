#ifndef KERNEL_DIAGNOSTICS_LATENCY_H
#define KERNEL_DIAGNOSTICS_LATENCY_H
#include <stdint.h>

// V2d M1-A: bounded latency accumulator, filled in the hot path, reported once at exit.
// Samples are clock ticks; buckets are powers of four: [0,64), [64,256), ... , [262144, inf).
#define KERNEL_LATENCY_BUCKETS 8U
typedef struct {
    uint64_t  Count, Min, Max, Total, Saturated;
    uint64_t  Buckets[KERNEL_LATENCY_BUCKETS];
} KERNEL_LATENCY;

uint32_t kernel_latency_bucket(uint64_t ticks);
void kernel_latency_add(KERNEL_LATENCY *owner, uint64_t ticks);
int kernel_latency_valid(const KERNEL_LATENCY *owner);
// Prints "<name>_COUNT/_MIN/_MAX/_TOTAL/_SATURATED/_B0.._B7: value" lines.
int kernel_latency_report(const char *name, const KERNEL_LATENCY *owner);
#endif
