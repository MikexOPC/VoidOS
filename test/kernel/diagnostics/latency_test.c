#include "kernel/diagnostics/latency.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks, lines, fail_at;
#define CHECK(x) do { ++checks; if (!(x)) { printf("FAIL LATENCY:%u\n", (unsigned)__LINE__); exit(1); } } while (0)

// ====== log mocks ======

int kernel_text(const char *text) { CHECK(text != 0); return ++lines != fail_at; }
int kernel_number(uint64_t value) { (void)value; return 1; }

// ====== main ======

int main(void)
{
    KERNEL_LATENCY owner;

    // Bucket edges: powers of four from 64.
    const uint64_t samples[][2] = {{0, 0}, {63, 0}, {64, 1}, {255, 1}, {256, 2}, {1023, 2}, {1024, 3},
        {4095, 3}, {4096, 4}, {16384, 5}, {65536, 6}, {262143, 6}, {262144, 7}, {UINT64_MAX, 7}};
    for (unsigned index = 0; index < sizeof(samples) / sizeof(samples[0]); ++index)
        CHECK(kernel_latency_bucket(samples[index][0]) == samples[index][1]);

    memset(&owner, 0, sizeof(owner));
    CHECK(kernel_latency_valid(&owner) && !kernel_latency_valid(0));
    kernel_latency_add(&owner, 500); kernel_latency_add(&owner, 20); kernel_latency_add(&owner, 70000);
    CHECK(owner.Count == 3 && owner.Min == 20 && owner.Max == 70000 && owner.Total == 70520);
    CHECK(owner.Buckets[0] == 1 && owner.Buckets[2] == 1 && owner.Buckets[6] == 1 && kernel_latency_valid(&owner));

    for (unsigned cycle = 0; cycle < 10000; ++cycle) kernel_latency_add(&owner, cycle * 37ULL);
    CHECK(owner.Count == 10003 && owner.Min == 0 && kernel_latency_valid(&owner));

    // Saturation: the total never wraps and the sample is counted as saturated only.
    KERNEL_LATENCY full = owner;
    full.Total = UINT64_MAX - 5;
    kernel_latency_add(&full, 10);
    CHECK(full.Count == owner.Count && full.Saturated == 1 && full.Total == UINT64_MAX - 5);
    full = owner; full.Count = UINT64_MAX;
    kernel_latency_add(&full, 1); CHECK(full.Saturated == 1);
    kernel_latency_add(0, 1);

    // Corrupted accumulators are refused by the validity check and the report.
    KERNEL_LATENCY bad = owner; ++bad.Buckets[3]; CHECK(!kernel_latency_valid(&bad) && !kernel_latency_report("X", &bad));
    bad = owner; bad.Min = bad.Max + 1; CHECK(!kernel_latency_valid(&bad));
    bad = owner; bad.Total = 0; CHECK(!kernel_latency_valid(&bad));
    memset(&bad, 0, sizeof(bad)); bad.Max = 1; CHECK(!kernel_latency_valid(&bad));

    // Report: 13 lines of 4 text calls each; every failing call stops it.
    lines = 0; fail_at = 0;
    CHECK(kernel_latency_report("KERNEL_VGPU_PRESENT_PM", &owner) && lines == 13 * 3);
    for (fail_at = 1; fail_at <= 13 * 3; ++fail_at) { lines = 0; CHECK(!kernel_latency_report("N", &owner)); }
    CHECK(!kernel_latency_report(0, &owner));

    printf("PASS %u latency checks; bucket edges, min/max/total, saturation, corruption and report failures\n", checks);
    return 0;
}
