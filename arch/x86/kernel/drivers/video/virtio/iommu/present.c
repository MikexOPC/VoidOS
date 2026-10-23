#include "session.h"
#include "arch/x86/kernel/drivers/video/virtio/queues/native.h"
#include "arch/x86/kernel/drivers/video/framebuffer/pixels/session.h"
#include "kernel/drivers/video/virtio/resources/scanout.h"
#include "kernel/diagnostics/log.h"
#include "kernel/diagnostics/latency.h"
static X86_VGPU_TRANSPORT transport;
static KERNEL_VGPU_IO io;
static X86_VGPU_QUEUE_TRANSPORT native;
static VGPU_QUEUE queue;
static VGPU_RESOURCE resource;
static X86_FB_TARGET target;
static const X86_SURFACE_MAPPING *bound;
static BOOT_U64 presents;
static BOOT_U32 presenting, redraw, failures, reset_done;
// V2d M1-A: whole present (TRANSFER + FLUSH, fenced), the FLUSH completion wait alone, and the
// presented area in pixels. PM ticks at 3.579545 MHz.
static KERNEL_LATENCY present_ticks, flush_ticks, present_area;

// ====== target_private ======

// The console target stays valid only while the surface is the registered, published one.
static int target_private(const X86_FB_TARGET *candidate)
{
    return candidate == &target && bound && x86_surface_owner() == &bound->Authority &&
        x86_vgpu_surface_check(&bound->Authority);
}

// ====== reset_private ======

// VirtIO 1.2 2.4: status 0 resets the device; confirmed before anything else is released.
// The transport is ended (which recaptures the paging inventory) only outside the session tasks:
// inside them the live stacks are mapped and the console accounting keeps its own baseline.
static int reset_private(int final)
{
    KERNEL_VGPU_NEGOTIATION result = {0};

    queue.State = 0;
    native.Self = 0;

    if (!transport.Self) return 1;

    if (!reset_done && !kernel_vgpu_reset(&io, &result)) return 0;

    reset_done = 1;
    return !final || x86_vgpu_transport_end(&transport);
}

// ====== fallback_private ======

// Z4-A: first error → stop presenting, reset the GPU, back to GOP with a full console redraw.
static int fallback_private(const char *reason)
{
    int reset;

    if (x86_fb_target() == &target) (void)x86_fb_target_redirect(&target, 0);

    presenting = 0;
    redraw = 1;
    ++failures;
    reset = reset_private(0);
    return kernel_log(2, reason) && reset;
}

// ====== x86_vgpu_present_open ======

// Z5-A: inside the V2b start, after TE=1 and before the watchdog. Returns 0 only when the GPU
// reset could not be confirmed; a refused or failed bring-up leaves the console on GOP.
int x86_vgpu_present_open(X86_VGPU_MAPPING *gpu, const KERNEL_ACPI_DIRECTORY *directory,
    X86_DMA_ARENA *arena, X86_SURFACE_MAPPING *surface)
{
    VGPU_QUEUE_IO_CONTEXT access = {0};
    VGPU_BUFFERS buffers = {0};
    KERNEL_VGPU_NEGOTIATION result = {0};

    if (presenting || transport.Self) return 0;

    volatile unsigned char *bytes = x86_dma_arena_bytes(arena);

    if (!bytes || !x86_vgpu_transport_open(gpu, directory, &transport, &io)) return kernel_log(2, "KERNEL_VGPU_PRESENT_OPEN_FAILED");

    io.Platform = 1;
    buffers.Bits = gpu->Authority.PhysicalBits;

    for (uint32_t index = 0; index < 4; ++index) {
        buffers.Views[index] = bytes + 4096ULL * index;
        buffers.Physical[index] = arena->Authority.Data.Base + 4096ULL * index;
        buffers.Device[index] = x86_vgpu_iommu_device(buffers.Physical[index], 4096, 3);
    }

    BOOT_U64 backing = x86_vgpu_iommu_device(surface->Authority.Data.Base, surface->Authority.Bytes, 1);

    if (!x86_vgpu_queue_open(&transport, &native, &access) || !kernel_vgpu_negotiate(&io, &result) ||
        !buffers.Device[0] || !buffers.Device[1] || !buffers.Device[2] || !buffers.Device[3] || !backing ||
        !kernel_vgpu_queue_start(&queue, &access, &buffers) || !kernel_vgpu_queue_exchange(&queue)
    )
    return fallback_private("KERNEL_VGPU_PRESENT_START_FAILED");

    // Geometry A: the resource takes the scanout size from GET_DISPLAY_INFO (VirtIO 1.2 5.7.6.8),
    // packed B8G8R8X8 (stride = width, the pixel layer's format 1); it must fit the surface, which
    // the session sized for X86_VGPU_SURFACE_WIDTH x HEIGHT before the display was known.
    BOOT_U64 needed = kernel_vgpu_surface_bytes(queue.Width, queue.Height);

    if (!needed || needed > surface->Authority.Bytes) return fallback_private("KERNEL_VGPU_PRESENT_GEOMETRY");

    if (!kernel_vgpu_resource_open(&queue, &resource, surface->Authority.Data.Base, backing, queue.Width, queue.Height) ||
        !kernel_vgpu_scanout_bind(&queue, &resource)
    )
    return fallback_private("KERNEL_VGPU_PRESENT_SCANOUT_FAILED");

    bound = surface;
    target = (X86_FB_TARGET){(BOOT_U64)&target, X86_SURFACE_VIRTUAL, surface->Authority.Bytes,
        queue.Width, queue.Height, queue.Width, 0, target_private};

    if (!x86_fb_target_redirect(0, &target)) return fallback_private("KERNEL_VGPU_PRESENT_TARGET_FAILED");

    presenting = 1;
    redraw = 1;
    return kernel_text("KERNEL_VGPU_PRESENT_SURFACE: ") && kernel_number(surface->Authority.Data.Base) && kernel_text("\n") &&
        kernel_text("KERNEL_VGPU_PRESENT_GEOMETRY: ") && kernel_number(queue.Width) && kernel_text("x") &&
        kernel_number(queue.Height) && kernel_text("\n") && kernel_log(0, "KERNEL_VGPU_PRESENTING");
}

// ====== x86_vgpu_present_rect ======

// Z2-A: one TRANSFER_TO_HOST_2D + RESOURCE_FLUSH of the dirty rectangle, fenced and polled.
int x86_vgpu_present_rect(BOOT_U32 x, BOOT_U32 y, BOOT_U32 width, BOOT_U32 height)
{
    if (!presenting) return 1;

    ++presents;

#ifdef KERNEL_TEST_VGPU_PRESENT_FAULT
    // Test-only: the N-th present behaves as a failed command to exercise the fallback path.
    if (presents == KERNEL_TEST_VGPU_PRESENT_FAULT) return fallback_private("KERNEL_VGPU_PRESENT_FALLBACK");
#endif

    uint32_t start = 0, end = 0;
    int clocked = io.Clock(io.Context, &start);

    if (kernel_vgpu_scanout_update(&queue, &resource, x, y, width, height)) {
        if (clocked && io.Clock(io.Context, &end)) {
            kernel_latency_add(&present_ticks, (end - start) & io.Mask);
            kernel_latency_add(&flush_ticks, queue.LastWait);
        }

        kernel_latency_add(&present_area, (BOOT_U64)width * height);
        return 1;
    }

    (void)(kernel_text("KERNEL_VGPU_PRESENT_RECT: ") && kernel_number(((BOOT_U64)x << 48) | ((BOOT_U64)y << 32) | ((BOOT_U64)width << 16) | height) &&
        kernel_text("\nKERNEL_VGPU_PRESENT_QUEUE_FAULT: ") && kernel_number(queue.Fault) && kernel_text("\n"));
    return fallback_private("KERNEL_VGPU_PRESENT_FALLBACK");
}

// ====== x86_vgpu_present_abandon ======

int x86_vgpu_present_abandon(void)
{
    return presenting ? fallback_private("KERNEL_VGPU_PRESENT_ABANDONED") : 1;
}

// ====== x86_vgpu_present_redraw ======

int x86_vgpu_present_redraw(void)
{
    BOOT_U32 pending = redraw;

    redraw = 0;
    return (int)pending;
}

// ====== x86_vgpu_present_close ======

// Exit: scanout off, detach and unref, then the confirmed reset (VirtIO 1.2 5.7.6.8 order).
int x86_vgpu_present_close(void)
{
    int closed = 1;

    if (!kernel_text("KERNEL_VGPU_PRESENTS: ") || !kernel_number(presents) || !kernel_text("\nKERNEL_VGPU_PRESENT_FAILURES: ") ||
        !kernel_number(failures) || !kernel_text("\n") ||
        !kernel_latency_report("KERNEL_VGPU_PRESENT_PM", &present_ticks) ||
        !kernel_latency_report("KERNEL_VGPU_FLUSH_WAIT_PM", &flush_ticks) ||
        !kernel_latency_report("KERNEL_VGPU_PRESENT_AREA", &present_area)
    )
    return 0;

    if (presenting) {
        if (x86_fb_target() == &target) (void)x86_fb_target_redirect(&target, 0);

        closed = kernel_vgpu_scanout_unbind(&queue, &resource) && kernel_vgpu_resource_close(&queue, &resource);
        presenting = 0;
    }

    if (!reset_private(1)) return 0;

    transport = (X86_VGPU_TRANSPORT){0};
    reset_done = 0;
    bound = 0;
    return closed || kernel_log(2, "KERNEL_VGPU_PRESENT_CLOSE_INCOMPLETE");
}
