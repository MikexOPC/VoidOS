#include "session.h"
#include "arch/x86/kernel/drivers/video/virtio/integration/kernel.h"
#include "kernel/diagnostics/log.h"
#include "kernel/diagnostics/latency.h"
#include <intrin.h>
static X86_DMA_ARENA arena;
static X86_SURFACE_MAPPING surface;
static BOOT_U64 budget, polls, held;
static BOOT_U32 state;
// V2d: fault scan per flush, and the IF=0 sections of start and stop (one sample each).
static KERNEL_LATENCY poll_ticks, quiet_ticks;
// ACPI 6.5 4.8.3.3 PM timer, kept after enable so the stop section is timed after the QI is gone.
static BOOT_U32 clock_port, clock_mask;

// ====== ticks_private ======

static BOOT_U64 ticks_private(void)
{
    return clock_port ? x86_pm_read((unsigned short)clock_port) & clock_mask : ~0ULL;
}

// ====== elapsed_private ======

static BOOT_U64 elapsed_private(BOOT_U64 start)
{
    BOOT_U64 now = ticks_private();

    return start == ~0ULL || now == ~0ULL ? ~0ULL : (now - start) & clock_mask;
}

// ====== number_private ======

static int number_private(const char *label, BOOT_U64 value)
{
    return kernel_text(label) && kernel_number(value) && kernel_text("\n");
}

// ====== quiet_begin / quiet_end ======

// Y1-A: VT-d structures are written through page-window borrows, which interrupt paths also
// use; on this single CPU, IF=0 around those sections keeps every borrow exclusive.
static BOOT_U64 quiet_begin(BOOT_U64 *ticks)
{
    BOOT_U64 flags = __readeflags();
    _disable();
    *ticks = ticks_private();
    return flags;
}

static void quiet_end(BOOT_U64 flags, BOOT_U64 ticks)
{
    BOOT_U64 elapsed = elapsed_private(ticks);

    if (elapsed != ~0ULL) kernel_latency_add(&quiet_ticks, elapsed);

    if (flags & 512) _enable();
}

// ====== release_private ======

// Translation never published or already off: give back surface and arena, check the budget.
static int release_private(X86_VGPU_MAPPING *gpu)
{
    if (surface.Self && !x86_vgpu_surface_unbind(&surface)) return 0;

    if (arena.Self && !x86_dma_arena_unbind(&arena)) return 0;

    return gpu->Window->Pool->FreePages == budget;
}

// ====== x86_vgpu_session_start ======

int x86_vgpu_session_start(const KERNEL_ACPI_DIRECTORY *directory)
{
    X86_VGPU_MAPPING *gpu = x86_vgpu_kernel_mapping();

    if (state != X86_VGPU_SESSION_OFF || arena.Self || surface.Self) return 0;

    if (!gpu || !gpu->Context->Info.Gpu.Present) return kernel_log(1, "KERNEL_VGPU_SESSION_ABSENT");

    const OS_FRAMEBUFFER *fb = &gpu->Context->Info.Framebuffer;
    BOOT_U64 since = 0, flags = quiet_begin(&since);
    int route = x86_vgpu_iommu_route(gpu, directory);

    if (route != X86_VGPU_IOMMU_READY) {
        quiet_end(flags, since);
        return kernel_log(route == X86_VGPU_IOMMU_ABSENT ? 1 : 2,
            route == X86_VGPU_IOMMU_ABSENT ? "KERNEL_VGPU_SESSION_UNTRANSLATED" : "KERNEL_VGPU_SESSION_ROUTE_FAILED");
    }

    budget = gpu->Window->Pool->FreePages;

    // V2c draws the console into this surface; until then it only sizes the GPU domain.
    BOOT_U32 step = 1;

    if (!fb->Width || !fb->Height ||
        !x86_dma_arena_bind(gpu->Context, gpu->Window, gpu->Lapic, gpu->Inventory, gpu->Capture, 4, &arena) ||
        (step = 2, !x86_vgpu_surface_bind(gpu, fb->Width, fb->Height, &surface)) ||
        (step = 3, !x86_vgpu_iommu_enable(&arena.Authority.Data, &surface.Authority.Data))
    ) {
        (void)number_private("KERNEL_VGPU_SESSION_FAILED_STEP: ", step);
        int released = x86_vgpu_iommu_disable() && release_private(gpu);

        state = released ? X86_VGPU_SESSION_OFF : X86_VGPU_SESSION_RETAINED;
        quiet_end(flags, since);
        return released && kernel_log(2, "KERNEL_VGPU_SESSION_ENABLE_FAILED");
    }

    state = X86_VGPU_SESSION_TRANSLATING;

    if (!x86_vgpu_iommu_clock(&clock_port, &clock_mask)) clock_port = 0;

    // V2c: the console moves to the GPU; a refused bring-up keeps GOP, an unconfirmed reset retains.
#ifndef KERNEL_PROFILE_VGPU_NO_PRESENT
    if (!x86_vgpu_present_open(gpu, directory, &arena, &surface)) state = X86_VGPU_SESSION_RETAINED;
#else
    // Profile image B: translation, arena and surface as in V2c, but the console stays on GOP.
    (void)directory;
#endif

    held = budget - gpu->Window->Pool->FreePages;
    quiet_end(flags, since);

    if (state == X86_VGPU_SESSION_RETAINED) return kernel_log(3, "KERNEL_VGPU_SESSION_RETAINED") && 0;

    return number_private("KERNEL_VGPU_SESSION_BUDGET: ", budget) && kernel_log(0, "KERNEL_VGPU_SESSION_TRANSLATING");
}

// ====== x86_vgpu_session_poll ======

int x86_vgpu_session_poll(void)
{
    if (state != X86_VGPU_SESSION_TRANSLATING) return 1;

    ++polls;

    BOOT_U64 since = ticks_private();
    int quiet = x86_vgpu_iommu_quiet();
    BOOT_U64 elapsed = elapsed_private(since);

    if (elapsed != ~0ULL) kernel_latency_add(&poll_ticks, elapsed);

    if (quiet) return 1;

    // X5-A: a GPU fault (or unreadable fault registers) withdraws the GPU; the console returns to GOP.
    state = X86_VGPU_SESSION_COMPROMISED;
    return kernel_log(2, "KERNEL_VGPU_SESSION_GPU_FAULT") && x86_vgpu_present_abandon();
}

// ====== x86_vgpu_session_stop ======

int x86_vgpu_session_stop(void)
{
    X86_VGPU_MAPPING *gpu = x86_vgpu_kernel_mapping();

    if (state == X86_VGPU_SESSION_OFF) return 1;

    if (!gpu || state == X86_VGPU_SESSION_RETAINED || !number_private("KERNEL_VGPU_SESSION_POLLS: ", polls) ||
        !kernel_latency_report("KERNEL_VGPU_POLL_PM", &poll_ticks)
    )
    return 0;

    // The GPU is stopped and reset before the translation it uses is turned off.
    if (!x86_vgpu_present_close()) {
        state = X86_VGPU_SESSION_RETAINED;
        return 0;
    }

    BOOT_U64 since = 0, flags = quiet_begin(&since);

    if (!x86_vgpu_iommu_disable()) {
        state = X86_VGPU_SESSION_RETAINED;
        quiet_end(flags, since);
        return 0;
    }

    int released = release_private(gpu);

    state = released ? X86_VGPU_SESSION_OFF : X86_VGPU_SESSION_RETAINED;

    if (released) held = 0;

    quiet_end(flags, since);
    // IF=0 sections: only the stop one is timed; the start one begins before the PM clock is captured.
    return released && kernel_latency_report("KERNEL_VGPU_QUIET_PM", &quiet_ticks) &&
        number_private("KERNEL_VGPU_SESSION_BUDGET_AFTER: ", gpu->Window->Pool->FreePages) &&
        kernel_log(0, "KERNEL_VGPU_SESSION_RELEASED");
}

// ====== x86_vgpu_session_state ======

BOOT_U32 x86_vgpu_session_state(void) { return state; }

// ====== x86_vgpu_session_pages ======

BOOT_U64 x86_vgpu_session_pages(void) { return held; }
