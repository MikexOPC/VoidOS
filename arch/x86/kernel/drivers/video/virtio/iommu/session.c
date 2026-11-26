#include "session.h"
#include "kernel/diagnostics/log.h"
#include <intrin.h>
#ifdef KERNEL_AP_RUNTIME
#include "arch/x86/kernel/cpu/percpu/service/native/native.h"
#endif

// ====== ticks_private ======

static BOOT_U64 ticks_private(const X86_VGPU_SESSION *self)
{
#ifdef KERNEL_VGPU_IOMMU
    return self->ClockPort ? x86_pm_read((unsigned short)self->ClockPort) & self->ClockMask : ~0ULL;
#else
    (void)self;
    return ~0ULL;
#endif
}

// ====== elapsed_private ======

static BOOT_U64 elapsed_private(const X86_VGPU_SESSION *self, BOOT_U64 start)
{
    BOOT_U64 now = ticks_private(self);

    return start == ~0ULL || now == ~0ULL ? ~0ULL : (now - start) & self->ClockMask;
}

// ====== number_private ======

static int number_private(const char *label, BOOT_U64 value)
{
    return kernel_text(label) && kernel_number(value) && kernel_text("\n");
}

// ====== quiet_begin / quiet_end ======

// Y1-A: VT-d structures are written through page-window borrows, which interrupt paths also
// use; on this single CPU, IF=0 around those sections keeps every borrow exclusive.
static BOOT_U64 quiet_begin(const X86_VGPU_SESSION *self, BOOT_U64 *ticks)
{
    BOOT_U64 flags = __readeflags();
    _disable();
    *ticks = ticks_private(self);
    return flags;
}

static void quiet_end(X86_VGPU_SESSION *self, BOOT_U64 flags, BOOT_U64 ticks)
{
    BOOT_U64 elapsed = elapsed_private(self, ticks);

    if (elapsed != ~0ULL) kernel_latency_add(&self->QuietTicks, elapsed);

    if (flags & 512) _enable();
}

// ====== release_private ======

// Translation never published or already off: give back surface and arena, check the budget.
static int release_private(X86_VGPU_SESSION *self, X86_VGPU_MAPPING *gpu)
{
    if (self->Surface.Self && !x86_vgpu_surface_unbind(&self->Surface)) return 0;

    if (self->Arena.Self && !x86_dma_arena_unbind(&self->Arena)) return 0;

    return kernel_physical_budget_matches(gpu->Window->Pool, self->Budget);
}

// ====== x86_vgpu_session_start ======

int x86_vgpu_session_start(X86_VGPU_SESSION *self, X86_VGPU_MAPPING *gpu, const KERNEL_ACPI_DIRECTORY *directory)
{
    if (self->State != X86_VGPU_SESSION_OFF || self->Arena.Self || self->Surface.Self) return 0;

    if (!gpu || !gpu->Context->Info.Gpu.Present) return kernel_log(1, "KERNEL_VGPU_SESSION_ABSENT");

    const OS_FRAMEBUFFER *fb = &gpu->Context->Info.Framebuffer;
    BOOT_U64 since = 0, flags = quiet_begin(self, &since);
    self->DirectDma = 0U;
#ifdef KERNEL_VGPU_IOMMU
    int route = x86_vgpu_iommu_route(gpu, directory);

    if (route == X86_VGPU_IOMMU_ABSENT) {
        self->DirectDma = 1U;
    } else if (route != X86_VGPU_IOMMU_READY) {
        quiet_end(self, flags, since);
        return kernel_log(2, "KERNEL_VGPU_SESSION_ROUTE_FAILED");
    }
#else
    self->DirectDma = 1U;
#endif

    if (!kernel_physical_budget_read(gpu->Window->Pool, &self->Budget)) {
        quiet_end(self, flags, since);
        return 0;
    }

    BOOT_U32 cap_w = self->DirectDma ? 1280U : X86_VGPU_SURFACE_WIDTH;
    BOOT_U32 cap_h = self->DirectDma ? 800U : X86_VGPU_SURFACE_HEIGHT;
    BOOT_U32 surf_w = fb->Width > cap_w ? fb->Width : cap_w;
    BOOT_U32 surf_h = fb->Height > cap_h ? fb->Height : cap_h;
    BOOT_U32 step = 1;

    int iommu_ok = 1;
    BOOT_U32 arena_pages = self->DirectDma ? 8U : 4U;
    if (!x86_dma_arena_bind(gpu->Context, gpu->Window, gpu->Lapic, gpu->Inventory, gpu->Capture, arena_pages, &self->Arena) ||
        (step = 2, !x86_vgpu_surface_bind(gpu, surf_w, surf_h, &self->Surface))) {
        iommu_ok = 0;
    }
#ifdef KERNEL_VGPU_IOMMU
    if (iommu_ok && !self->DirectDma) {
        step = 3;
        iommu_ok = x86_vgpu_iommu_enable(&self->Arena.Authority.Data, &self->Surface.Authority.Data);
    }
#endif
    if (!iommu_ok) {
        (void)number_private("KERNEL_VGPU_SESSION_FAILED_STEP: ", step);
        int disabled = 1;
#ifdef KERNEL_VGPU_IOMMU
        if (!self->DirectDma)
            disabled = x86_vgpu_iommu_disable();
#endif
        int released = disabled && release_private(self, gpu);

        self->State = released ? X86_VGPU_SESSION_OFF : X86_VGPU_SESSION_RETAINED;
        quiet_end(self, flags, since);
        return released && kernel_log(2, "KERNEL_VGPU_SESSION_ENABLE_FAILED");
    }

    self->State = X86_VGPU_SESSION_TRANSLATING;
    self->ClockPort = 0;
#ifdef KERNEL_VGPU_IOMMU
    if (!self->DirectDma && !x86_vgpu_iommu_clock(&self->ClockPort, &self->ClockMask)) self->ClockPort = 0;
#endif

    // V2c: the console moves to the GPU; a refused bring-up keeps GOP, an unconfirmed reset retains.
#ifndef KERNEL_PROFILE_VGPU_NO_PRESENT
    self->Present.DirectDma = self->DirectDma;
    if (!x86_vgpu_present_open(&self->Present, gpu, directory, &self->Arena, &self->Surface)) self->State = X86_VGPU_SESSION_RETAINED;
#else
    // Profile image B: translation, arena and surface as in V2c, but the console stays on GOP.
    (void)directory;
#endif

    BOOT_U64 available;
    if (!kernel_physical_budget_read(gpu->Window->Pool, &available) || available > self->Budget) {
        self->State = X86_VGPU_SESSION_RETAINED;
        quiet_end(self, flags, since);
        return 0;
    }
    self->Held = self->Budget - available;
    // G5 V3: the session declares what it holds; the console accounting subtracts the total.
    if (self->Held && !kernel_holdings_register(&self->Holding, self->Held)) self->State = X86_VGPU_SESSION_RETAINED;
    quiet_end(self, flags, since);

    if (self->State == X86_VGPU_SESSION_RETAINED) return kernel_log(3, "KERNEL_VGPU_SESSION_RETAINED") && 0;

    return number_private("KERNEL_VGPU_SESSION_BUDGET: ", self->Budget) && kernel_log(0, "KERNEL_VGPU_SESSION_TRANSLATING");
}

// ====== x86_vgpu_session_poll ======

int x86_vgpu_session_poll(X86_VGPU_SESSION *self)
{
    if (self->State != X86_VGPU_SESSION_TRANSLATING) return 1;

    ++self->Polls;

    if (self->DirectDma) return 1;

#ifdef KERNEL_VGPU_IOMMU
    BOOT_U64 since = ticks_private(self);
    int quiet = x86_vgpu_iommu_quiet();
    BOOT_U64 elapsed = elapsed_private(self, since);

    if (elapsed != ~0ULL) kernel_latency_add(&self->PollTicks, elapsed);

    if (quiet) return 1;

    // X5-A: a GPU fault (or unreadable fault registers) withdraws the GPU; the console returns to GOP.
    self->State = X86_VGPU_SESSION_COMPROMISED;
    return kernel_log(2, "KERNEL_VGPU_SESSION_GPU_FAULT") && x86_vgpu_present_abandon(&self->Present);
#else
    return 1;
#endif
}

// ====== x86_vgpu_session_stop ======

int x86_vgpu_session_stop(X86_VGPU_SESSION *self, X86_VGPU_MAPPING *gpu)
{
    if (self->State == X86_VGPU_SESSION_OFF) return 1;

    if (!gpu || self->State == X86_VGPU_SESSION_RETAINED || !number_private("KERNEL_VGPU_SESSION_POLLS: ", self->Polls) ||
        !kernel_latency_report("KERNEL_VGPU_POLL_PM", &self->PollTicks)
    )
    return 0;

#ifdef KERNEL_AP_RUNTIME
    // K4-B evidence: AP flushes acknowledged while the session releases its CPU mappings.
    if (!number_private("KERNEL_VGPU_SESSION_SHOOTDOWNS_BEFORE: ", xis_native_shootdowns())) return 0;
#endif

    // The GPU is stopped and reset before the translation it uses is turned off.
    if (!x86_vgpu_present_close(&self->Present)) {
        self->State = X86_VGPU_SESSION_RETAINED;
        return 0;
    }

    BOOT_U64 since = 0, flags = quiet_begin(self, &since);

#ifdef KERNEL_VGPU_IOMMU
    if (!self->DirectDma && !x86_vgpu_iommu_disable()) {
        self->State = X86_VGPU_SESSION_RETAINED;
        quiet_end(self, flags, since);
        return 0;
    }
#endif

    int released = release_private(self, gpu);

    self->State = released ? X86_VGPU_SESSION_OFF : X86_VGPU_SESSION_RETAINED;

    if (released) {
        self->Held = 0;
        if (self->Holding.Self) released = kernel_holdings_unregister(&self->Holding);
    }

    quiet_end(self, flags, since);
    // IF=0 sections: only the stop one is timed; the start one begins before the PM clock is captured.
    BOOT_U64 after;
    if (!released || !kernel_physical_budget_read(gpu->Window->Pool, &after)) return 0;
    return released && kernel_latency_report("KERNEL_VGPU_QUIET_PM", &self->QuietTicks) &&
        number_private("KERNEL_VGPU_SESSION_BUDGET_AFTER: ", after) &&
#ifdef KERNEL_AP_RUNTIME
        number_private("KERNEL_VGPU_SESSION_SHOOTDOWNS_AFTER: ", xis_native_shootdowns()) &&
#endif
        kernel_log(0, "KERNEL_VGPU_SESSION_RELEASED");
}

// ====== x86_vgpu_session_state ======

BOOT_U32 x86_vgpu_session_state(const X86_VGPU_SESSION *self) { return self->State; }

// ====== x86_vgpu_session_pages ======

BOOT_U64 x86_vgpu_session_pages(const X86_VGPU_SESSION *self) { return self->Held; }

// ====== display operations ======

// G6 O3: Check is the fault scan (console stage 1), the rest forward to the owned present.
static int display_check(void *self) { return x86_vgpu_session_poll(self); }
static int display_poll(void *self) { return x86_vgpu_present_poll(&((X86_VGPU_SESSION *)self)->Present); }
static int display_busy(void *self) { return x86_vgpu_present_busy(&((X86_VGPU_SESSION *)self)->Present); }
static int display_redraw(void *self) { return x86_vgpu_present_redraw(&((X86_VGPU_SESSION *)self)->Present); }

static int display_rect(void *self, uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
    return x86_vgpu_present_rect(&((X86_VGPU_SESSION *)self)->Present, x, y, width, height);
}

static int display_cursor(void *self, uint32_t x, uint32_t y, uint32_t visible)
{
    return x86_vgpu_present_cursor(&((X86_VGPU_SESSION *)self)->Present, x, y, visible);
}

static const KERNEL_DISPLAY_OPS display_ops = {display_check, display_poll, display_busy, display_redraw, display_rect, display_cursor};

// ====== x86_vgpu_session_display ======

KERNEL_DISPLAY x86_vgpu_session_display(X86_VGPU_SESSION *self) { return (KERNEL_DISPLAY){self, &display_ops}; }
