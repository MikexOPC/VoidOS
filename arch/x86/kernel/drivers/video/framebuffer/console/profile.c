#include "profile.h"
#include "arch/x86/kernel/timer/lapic/timer.h"
#include "kernel/diagnostics/latency.h"
#include "kernel/time/clock.h"
static KERNEL_LATENCY phases[X86_PROFILE_PHASES];
static uint32_t clock_port, clock_mask, active;

// ====== x86_profile_start ======

// ACPI 6.5 4.8.3.3: the PM timer counts at 3.579545 MHz with a 24- or 32-bit counter.
void x86_profile_start(uint32_t port, uint32_t mask)
{
    if (!port || port > 65535 || (mask != 0xffffff && mask != UINT32_MAX)) return;

    clock_port = port;
    clock_mask = mask;
    active = 1;
}

// ====== x86_profile_now ======

uint32_t x86_profile_now(void)
{
    return active ? x86_pm_read((unsigned short)clock_port) & clock_mask : 0;
}

// ====== x86_profile_add ======

void x86_profile_add(uint32_t phase, uint32_t start)
{
    if (!active || phase >= X86_PROFILE_PHASES) return;

    kernel_latency_add(&phases[phase], kernel_counter_delta(clock_mask, start, x86_profile_now()));
}

// ====== x86_profile_report ======

int x86_profile_report(void)
{
    static const char *const names[X86_PROFILE_PHASES] = {"KERNEL_PROFILE_FLUSH", "KERNEL_PROFILE_RENDER",
        "KERNEL_PROFILE_GROUP", "KERNEL_PROFILE_VERIFY", "KERNEL_PROFILE_TARGET", "KERNEL_PROFILE_PREPARE",
        "KERNEL_PROFILE_PRESENT", "KERNEL_PROFILE_POLL"};

    for (uint32_t index = 0; index < X86_PROFILE_PHASES; ++index)
        if (!kernel_latency_report(names[index], &phases[index])) return 0;

    return 1;
}
