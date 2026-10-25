#ifndef X86_VGPU_PRESENT_STATE_H
#define X86_VGPU_PRESENT_STATE_H
#include "present.h"
#include "kernel/diagnostics/log.h"

// Private to present/. R1-A: one update in flight. Phase 1 TRANSFER posted, phase 2 FLUSH posted;
// damage that arrives meanwhile is merged (lib/geometry, Limit 1) and presented next. R2: SLOW
// mark past 100 ms, fallback at 1 s. R3-A: after a timeout fallback, at most 3 bring-ups, 5 s apart.
#define PRESENT_SLOW_PM     (KERNEL_PM_TIMER_HZ / 10)
#define PRESENT_LIMIT_PM    KERNEL_PM_TIMER_HZ
#define RECOVER_SPACING_PM  (5ULL * KERNEL_PM_TIMER_HZ)
#define RECOVER_TRIES       3U

// lifecycle.c
int x86_vgpu_present_fallback(X86_VGPU_PRESENT *self, const char *reason);
const char *x86_vgpu_present_bringup(X86_VGPU_PRESENT *self);
// update.c
int x86_vgpu_present_fault(X86_VGPU_PRESENT *self);
int x86_vgpu_present_pump(X86_VGPU_PRESENT *self);
#endif
