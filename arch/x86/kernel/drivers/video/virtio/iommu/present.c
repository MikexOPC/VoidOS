#include "session.h"
#include "arch/x86/kernel/drivers/video/virtio/queues/native.h"
#include "arch/x86/kernel/drivers/video/framebuffer/pixels/session.h"
#include "kernel/drivers/video/virtio/resources/scanout.h"
#include "kernel/diagnostics/log.h"
#include "kernel/diagnostics/latency.h"
#include "lib/geometry/damage.h"
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
// Recovery R3-A: what the bring-up needs again after a reset inside the session tasks. The
// transport and the native queue stay open (queue open would recapture the inventory).
static X86_VGPU_QUEUE_TRANSPORT native_saved;
static VGPU_QUEUE_IO_CONTEXT access_saved;
static VGPU_BUFFERS buffers_saved;
static BOOT_U64 backing_saved;
static X86_SURFACE_MAPPING *surface_saved;
// R1-A: one update in flight. Phase 1 TRANSFER posted, phase 2 FLUSH posted; damage that arrives
// meanwhile is merged and presented next (right == x: nothing queued).
#define PRESENT_SLOW_PM  357955U
#define PRESENT_LIMIT_PM 3579545U
#define RECOVER_SPACING_PM (5ULL * 3579545U)
#define RECOVER_TRIES 3U
static BOOT_U32 phase, slow_marked, submitted_at;
static BOOT_U32 fly_x, fly_y, fly_w, fly_h;
// G1: queued damage through lib/geometry, Limit 1 (one envelope per update).
static OS_DAMAGE damage = {0, 1, {{0}}};
static BOOT_U32 recoverable, recoveries, recover_last;
static BOOT_U64 recover_elapsed, slows, timeouts, recovered;
#ifdef KERNEL_TEST_VGPU_PRESENT_STALL
// Test-only: present N holds its TRANSFER 1.2 s (timeout, fallback, recovery), present N+200 holds
// it 0.2 s (slow mark only). The device completed: only the observation is delayed.
static BOOT_U32 stall_hold;
#endif

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
    phase = 0;
    os_damage_clear(&damage);
    recover_elapsed = 0;
    if (!io.Clock(io.Context, &recover_last)) recoverable = 0;
    ++failures;
    reset = reset_private(0);
    return kernel_log(2, reason) && reset;
}

// ====== bringup_private ======

// After the transport and the native queue: negotiation, queue, display info, resource, scanout
// and the target. Returns the failure reason, 0 when presenting.
static const char *bringup_private(void)
{
    KERNEL_VGPU_NEGOTIATION result = {0};
    X86_SURFACE_MAPPING *surface = surface_saved;

    native = native_saved;
    queue = (VGPU_QUEUE){0};
    resource = (VGPU_RESOURCE){0};

    if (!kernel_vgpu_negotiate(&io, &result) || !kernel_vgpu_queue_start(&queue, &access_saved, &buffers_saved) ||
        !kernel_vgpu_queue_exchange(&queue)
    )
    return "KERNEL_VGPU_PRESENT_START_FAILED";

    reset_done = 0;

    // Geometry A: the resource takes the scanout size from GET_DISPLAY_INFO (VirtIO 1.2 5.7.6.8),
    // packed B8G8R8X8 (stride = width, the pixel layer's format 1); it must fit the surface, which
    // the session sized for X86_VGPU_SURFACE_WIDTH x HEIGHT before the display was known.
    BOOT_U64 needed = kernel_vgpu_surface_bytes(queue.Width, queue.Height);

    if (!needed || needed > surface->Authority.Bytes) return "KERNEL_VGPU_PRESENT_GEOMETRY";

    if (!kernel_vgpu_resource_open(&queue, &resource, surface->Authority.Data.Base, backing_saved, queue.Width, queue.Height) ||
        !kernel_vgpu_scanout_bind(&queue, &resource)
    )
    return "KERNEL_VGPU_PRESENT_SCANOUT_FAILED";

    bound = surface;
    target = (X86_FB_TARGET){(BOOT_U64)&target, X86_SURFACE_VIRTUAL, surface->Authority.Bytes,
        queue.Width, queue.Height, queue.Width, 0, target_private};

    if (!x86_fb_target_redirect(0, &target)) return "KERNEL_VGPU_PRESENT_TARGET_FAILED";

    presenting = 1;
    redraw = 1;
    phase = 0;
    return 0;
}

// ====== x86_vgpu_present_open ======

// Z5-A: inside the V2b start, after TE=1 and before the watchdog. Returns 0 only when the GPU
// reset could not be confirmed; a refused or failed bring-up leaves the console on GOP.
int x86_vgpu_present_open(X86_VGPU_MAPPING *gpu, const KERNEL_ACPI_DIRECTORY *directory,
    X86_DMA_ARENA *arena, X86_SURFACE_MAPPING *surface)
{
    VGPU_QUEUE_IO_CONTEXT access = {0};
    VGPU_BUFFERS buffers = {0};

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

    if (!x86_vgpu_queue_open(&transport, &native, &access) ||
        !buffers.Device[0] || !buffers.Device[1] || !buffers.Device[2] || !buffers.Device[3] || !backing
    )
    return fallback_private("KERNEL_VGPU_PRESENT_START_FAILED");

    native_saved = native;
    access_saved = access;
    buffers_saved = buffers;
    backing_saved = backing;
    surface_saved = surface;

    const char *failure = bringup_private();

    if (failure) return fallback_private(failure);

    return kernel_text("KERNEL_VGPU_PRESENT_SURFACE: ") && kernel_number(surface->Authority.Data.Base) && kernel_text("\n") &&
        kernel_text("KERNEL_VGPU_PRESENT_GEOMETRY: ") && kernel_number(queue.Width) && kernel_text("x") &&
        kernel_number(queue.Height) && kernel_text("\n") && kernel_log(0, "KERNEL_VGPU_PRESENTING");
}

// ====== fault_private ======

// A failed post or completion: report, then the Z4-A fallback. Only a timeout may be recovered.
static int fault_private(void)
{
    recoverable = queue.Fault == VGPU_QUEUE_TIME;
    if (recoverable) ++timeouts;

    (void)(kernel_text("KERNEL_VGPU_PRESENT_RECT: ") &&
        kernel_number(((BOOT_U64)fly_x << 48) | ((BOOT_U64)fly_y << 32) | ((BOOT_U64)fly_w << 16) | fly_h) &&
        kernel_text("\nKERNEL_VGPU_PRESENT_QUEUE_FAULT: ") && kernel_number(queue.Fault) && kernel_text("\nKERNEL_VGPU_PRESENT_PHASE: ") &&
        kernel_number(phase) && kernel_text("\n"));
    return fallback_private(recoverable ? "KERNEL_VGPU_PRESENT_TIMEOUT" : "KERNEL_VGPU_PRESENT_FALLBACK");
}

// ====== elapsed_private ======

static BOOT_U32 elapsed_private(void)
{
    uint32_t now = 0;

    return io.Clock(io.Context, &now) ? (now - submitted_at) & io.Mask : PRESENT_LIMIT_PM;
}

// ====== complete_private ======

// R1-A: observe the command in flight (phase 1 or 2) once. 1 completed, 0 pending, -1 fault.
static int complete_private(void)
{
    int result;

#ifdef KERNEL_TEST_VGPU_PRESENT_STALL
    BOOT_U32 stall = presents == KERNEL_TEST_VGPU_PRESENT_STALL ? 4295454U :
        presents == KERNEL_TEST_VGPU_PRESENT_STALL + 200 ? 715909U : 0;

    result = stall_hold ? 1 : kernel_vgpu_queue_poll(&queue, PRESENT_LIMIT_PM);

    if (result == 1 && phase == 1 && stall) {
        BOOT_U32 elapsed = elapsed_private();

        stall_hold = elapsed < stall;
        if (stall_hold && elapsed >= PRESENT_LIMIT_PM) { queue.Fault = VGPU_QUEUE_TIME; stall_hold = 0; return -1; }
        if (stall_hold) result = 0;
    }
#else
    result = kernel_vgpu_queue_poll(&queue, PRESENT_LIMIT_PM);
#endif

    if (result) return result;

    // R2: past 100 ms the update is marked slow once; the 1 s limit is enforced by the queue poll.
    if (!slow_marked && elapsed_private() >= PRESENT_SLOW_PM) {
        slow_marked = 1;
        ++slows;
        (void)kernel_log(2, "KERNEL_VGPU_PRESENT_SLOW");
    }

    return 0;
}

// ====== pump_private ======

// Advances the update in flight and starts the next one from the merged damage. Never waits.
static int pump_private(void)
{
    for (BOOT_U32 step = 0; presenting && step < 4; ++step) {
        if (!phase) {
            if (os_damage_empty(&damage)) return 1;

            ++presents;

#ifdef KERNEL_TEST_VGPU_PRESENT_FAULT
            // Test-only: the N-th present behaves as a failed command to exercise the fallback path.
            if (presents == KERNEL_TEST_VGPU_PRESENT_FAULT) { recoverable = 0; return fallback_private("KERNEL_VGPU_PRESENT_FALLBACK"); }
#endif

            OS_RECT box;

            (void)os_damage_bounds(&damage, &box);
            fly_x = box.X; fly_y = box.Y; fly_w = box.Width; fly_h = box.Height;
            os_damage_clear(&damage);

            if (!io.Clock(io.Context, &submitted_at) ||
                !kernel_vgpu_scanout_post(&queue, &resource, 0, fly_x, fly_y, fly_w, fly_h)
            )
            return fault_private();

            phase = 1;
            slow_marked = 0;
            continue;
        }

        int result = complete_private();

        if (result < 0) return fault_private();

        if (!result) return 1;

        if (phase == 1) {
            if (!kernel_vgpu_scanout_post(&queue, &resource, 1, fly_x, fly_y, fly_w, fly_h)) return fault_private();

            phase = 2;
            continue;
        }

        if (!kernel_vgpu_scanout_settle(&queue, &resource)) return fault_private();

        kernel_latency_add(&present_ticks, elapsed_private());
        kernel_latency_add(&flush_ticks, queue.LastWait);
        kernel_latency_add(&present_area, (BOOT_U64)fly_w * fly_h);
        phase = 0;
    }

    return 1;
}

// ====== recover_private ======

// R3-A: after a timeout fallback, at most 3 bring-ups per session, 5 s apart. The surface is
// cleared and presented whole; the console redraws its (possibly clipped) grid on it.
static int recover_private(void)
{
    uint32_t now = 0;

    if (presenting || !recoverable || recoveries >= RECOVER_TRIES || !transport.Self) return 1;

    if (!io.Clock(io.Context, &now)) return 1;

    recover_elapsed += (now - recover_last) & io.Mask;
    recover_last = now;
    if (recover_elapsed < RECOVER_SPACING_PM) return 1;

    ++recoveries;
    recoverable = 0;

    const char *failure = bringup_private();

    if (failure) return fallback_private(failure);

    // Not x86_vgpu_surface_pixels: it recaptures the paging inventory, which the session tasks must
    // not do (the console accounting keeps its own baseline). The authority check is enough here.
    if (!x86_vgpu_surface_check(&surface_saved->Authority)) return fallback_private("KERNEL_VGPU_PRESENT_RECOVERY_FAILED");

    volatile uint32_t *pixels = (volatile uint32_t *)X86_SURFACE_VIRTUAL;

    for (BOOT_U64 index = 0; index < (BOOT_U64)queue.Width * queue.Height; ++index) pixels[index] = 0;

    (void)os_damage_add(&damage, &(OS_RECT){0, 0, queue.Width, queue.Height});
    ++recovered;
    return kernel_log(1, "KERNEL_VGPU_PRESENT_RECOVERED") && pump_private();
}

// ====== x86_vgpu_present_rect ======

// Z2-A with R1-A: the dirty rectangle joins the queued damage; TRANSFER_TO_HOST_2D and
// RESOURCE_FLUSH are posted and observed on later calls, never waited for.
int x86_vgpu_present_rect(BOOT_U32 x, BOOT_U32 y, BOOT_U32 width, BOOT_U32 height)
{
    if (!presenting || !width || !height) return presenting ? pump_private() : 1;

    if (!os_damage_add(&damage, &(OS_RECT){x, y, width, height})) return 1;

    return pump_private();
}

// ====== x86_vgpu_present_poll ======

// Every console flush: progress of the update in flight, or the recovery after a timeout.
int x86_vgpu_present_poll(void) { return presenting ? pump_private() : recover_private(); }

// ====== x86_vgpu_present_busy ======

// The console keeps flushing while an update is in flight, damage waits or a recovery is due.
int x86_vgpu_present_busy(void)
{
    return presenting ? phase || !os_damage_empty(&damage) : recoverable && recoveries < RECOVER_TRIES && transport.Self;
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
        !kernel_latency_report("KERNEL_VGPU_PRESENT_AREA", &present_area) ||
        !kernel_text("KERNEL_VGPU_PRESENT_SLOWS: ") || !kernel_number(slows) || !kernel_text("\nKERNEL_VGPU_PRESENT_TIMEOUTS: ") ||
        !kernel_number(timeouts) || !kernel_text("\nKERNEL_VGPU_PRESENT_RECOVERIES: ") || !kernel_number(recovered) || !kernel_text("\n")
    )
    return 0;

    // The update in flight completes (bounded) before the scanout is disabled.
    for (BOOT_U32 sample = 0; presenting && phase && sample < io.Samples; ++sample) {
        int result = kernel_vgpu_queue_poll(&queue, PRESENT_LIMIT_PM);

        if (result < 0) { (void)fault_private(); break; }
        if (result) {
            phase = 0;
            if (!kernel_vgpu_scanout_settle(&queue, &resource)) { (void)fault_private(); break; }
        }
    }

    recoverable = 0;

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
