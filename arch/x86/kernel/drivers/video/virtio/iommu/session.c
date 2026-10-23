#include "session.h"
#include "arch/x86/kernel/drivers/video/virtio/integration/kernel.h"
#include "arch/x86/kernel/drivers/video/virtio/resources/mapping.h"
#include "arch/x86/mm/dma/arena.h"
#include "kernel/diagnostics/log.h"
#include <intrin.h>
static X86_DMA_ARENA arena;
static X86_SURFACE_MAPPING surface;
static BOOT_U64 budget, polls, held;
static BOOT_U32 state;

// ====== number_private ======

static int number_private(const char *label, BOOT_U64 value)
{
    return kernel_text(label) && kernel_number(value) && kernel_text("\n");
}

// ====== quiet_begin / quiet_end ======

// Y1-A: VT-d structures are written through page-window borrows, which interrupt paths also
// use; on this single CPU, IF=0 around those sections keeps every borrow exclusive.
static BOOT_U64 quiet_begin(void)
{
    BOOT_U64 flags = __readeflags();
    _disable();
    return flags;
}

static void quiet_end(BOOT_U64 flags)
{
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
    BOOT_U64 flags = quiet_begin();
    int route = x86_vgpu_iommu_route(gpu, directory);

    if (route != X86_VGPU_IOMMU_READY) {
        quiet_end(flags);
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
        quiet_end(flags);
        return released && kernel_log(2, "KERNEL_VGPU_SESSION_ENABLE_FAILED");
    }

    state = X86_VGPU_SESSION_TRANSLATING;
    held = budget - gpu->Window->Pool->FreePages;
    quiet_end(flags);
    return number_private("KERNEL_VGPU_SESSION_BUDGET: ", budget) && kernel_log(0, "KERNEL_VGPU_SESSION_TRANSLATING");
}

// ====== x86_vgpu_session_poll ======

int x86_vgpu_session_poll(void)
{
    if (state != X86_VGPU_SESSION_TRANSLATING) return 1;

    ++polls;

    if (x86_vgpu_iommu_quiet()) return 1;

    // X5-A: a GPU fault (or unreadable fault registers) withdraws the GPU; the console stays on GOP.
    state = X86_VGPU_SESSION_COMPROMISED;
    return kernel_log(2, "KERNEL_VGPU_SESSION_GPU_FAULT");
}

// ====== x86_vgpu_session_stop ======

int x86_vgpu_session_stop(void)
{
    X86_VGPU_MAPPING *gpu = x86_vgpu_kernel_mapping();

    if (state == X86_VGPU_SESSION_OFF) return 1;

    if (!gpu || state == X86_VGPU_SESSION_RETAINED || !number_private("KERNEL_VGPU_SESSION_POLLS: ", polls)) return 0;

    BOOT_U64 flags = quiet_begin();

    if (!x86_vgpu_iommu_disable()) {
        state = X86_VGPU_SESSION_RETAINED;
        quiet_end(flags);
        return 0;
    }

    int released = release_private(gpu);

    state = released ? X86_VGPU_SESSION_OFF : X86_VGPU_SESSION_RETAINED;

    if (released) held = 0;

    quiet_end(flags);
    return released && number_private("KERNEL_VGPU_SESSION_BUDGET_AFTER: ", gpu->Window->Pool->FreePages) &&
        kernel_log(0, "KERNEL_VGPU_SESSION_RELEASED");
}

// ====== x86_vgpu_session_state ======

BOOT_U32 x86_vgpu_session_state(void) { return state; }

// ====== x86_vgpu_session_pages ======

BOOT_U64 x86_vgpu_session_pages(void) { return held; }
