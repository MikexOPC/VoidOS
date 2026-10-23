#include "latency.h"
#include "log.h"

// ====== kernel_latency_bucket ======

uint32_t kernel_latency_bucket(uint64_t ticks)
{
    uint32_t bucket = 0;

    for (uint64_t edge = 64; bucket < KERNEL_LATENCY_BUCKETS - 1 && ticks >= edge; edge <<= 2) ++bucket;

    return bucket;
}

// ====== kernel_latency_add ======

// Never wraps: the count and the total stop at their maximum and Saturated records the overflow.
void kernel_latency_add(KERNEL_LATENCY *owner, uint64_t ticks)
{
    if (!owner) return;

    if (owner->Count == UINT64_MAX || owner->Total > UINT64_MAX - ticks) {
        if (owner->Saturated != UINT64_MAX) ++owner->Saturated;

        return;
    }

    if (!owner->Count || ticks < owner->Min) owner->Min = ticks;

    if (ticks > owner->Max) owner->Max = ticks;

    ++owner->Count;
    owner->Total += ticks;
    ++owner->Buckets[kernel_latency_bucket(ticks)];
}

// ====== kernel_latency_valid ======

int kernel_latency_valid(const KERNEL_LATENCY *owner)
{
    uint64_t sum = 0;

    if (!owner) return 0;

    for (uint32_t index = 0; index < KERNEL_LATENCY_BUCKETS; ++index) {
        if (owner->Buckets[index] > UINT64_MAX - sum) return 0;

        sum += owner->Buckets[index];
    }

    if (sum != owner->Count) return 0;

    if (!owner->Count) return !owner->Min && !owner->Max && !owner->Total;

    return owner->Min <= owner->Max && owner->Total >= owner->Max &&
        owner->Total / owner->Count >= owner->Min && owner->Total / owner->Count <= owner->Max;
}

// ====== number_private ======

static int number_private(const char *name, const char *field, uint64_t value)
{
    return kernel_text(name) && kernel_text(field) && kernel_number(value) && kernel_text("\n");
}

// ====== kernel_latency_report ======

int kernel_latency_report(const char *name, const KERNEL_LATENCY *owner)
{
    static const char *const fields[KERNEL_LATENCY_BUCKETS] = {"_B0: ", "_B1: ", "_B2: ", "_B3: ", "_B4: ", "_B5: ", "_B6: ", "_B7: "};

    if (!name || !kernel_latency_valid(owner) ||
        !number_private(name, "_COUNT: ", owner->Count) || !number_private(name, "_MIN: ", owner->Min) ||
        !number_private(name, "_MAX: ", owner->Max) || !number_private(name, "_TOTAL: ", owner->Total) ||
        !number_private(name, "_SATURATED: ", owner->Saturated)
    )
    return 0;

    for (uint32_t index = 0; index < KERNEL_LATENCY_BUCKETS; ++index)
        if (!number_private(name, fields[index], owner->Buckets[index])) return 0;

    return 1;
}
