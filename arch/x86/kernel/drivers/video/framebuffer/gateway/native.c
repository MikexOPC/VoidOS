#include "cell.h"
#include <intrin.h>

// ====== x86_fb_cell_clock_available ======

int x86_fb_cell_clock_available(void)
{
    int registers[4];
    __cpuid(registers, 0);
    if ((unsigned int)registers[0] < 1) return 0;
    __cpuid(registers, 1);
    return ((unsigned int)registers[3] & (1U << 4)) != 0;
}

// ====== x86_fb_cell_stamp ======

BOOT_U64 x86_fb_cell_stamp(void)
{
    int registers[4];
    BOOT_U64 value;
    _ReadWriteBarrier();
    __cpuid(registers, 0);
    value = __rdtsc();
    __cpuid(registers, 0);
    _ReadWriteBarrier();
    return value;
}

// ====== x86_fb_cell_suspend ======

BOOT_U64 x86_fb_cell_suspend(void)
{
    BOOT_U64 flags = __readeflags();
    _disable();
    _ReadWriteBarrier();
    return flags;
}

// ====== x86_fb_cell_resume ======

int x86_fb_cell_resume(BOOT_U64 flags)
{
    _ReadWriteBarrier();
    if (flags & 512) _enable(); else _disable();
    return (__readeflags() & 512) == (flags & 512);
}
