#include "state.h"

// One presentation per session (see state.h).
X86_VGPU_PRESENT x86_vgpu_present_state = {.Damage = {0, 1, {{0}}}};
static X86_VGPU_PRESENT *const state = &x86_vgpu_present_state;

// ====== target_private ======

// The console target stays valid only while the surface is the registered, published one.
static int target_private(const X86_FB_TARGET *candidate)
{
    return candidate == &state->Target && state->Bound && x86_surface_owner() == &state->Bound->Authority &&
        x86_vgpu_surface_check(&state->Bound->Authority);
}

// ====== reset_private ======

// VirtIO 1.2 2.4: status 0 resets the device; confirmed before anything else is released.
// The transport is ended (which recaptures the paging inventory) only outside the session tasks:
// inside them the live stacks are mapped and the console accounting keeps its own baseline.
static int reset_private(int final)
{
    KERNEL_VGPU_NEGOTIATION result = {0};

    state->Queue.State = 0;
    state->Native.Self = 0;

    if (!state->Transport.Self) return 1;

    if (!state->ResetDone && !kernel_vgpu_reset(&state->Io, &result)) return 0;

    state->ResetDone = 1;
    return !final || x86_vgpu_transport_end(&state->Transport);
}

// ====== x86_vgpu_present_fallback ======

// Z4-A: first error → stop presenting, reset the GPU, back to GOP with a full console redraw.
int x86_vgpu_present_fallback(const char *reason)
{
    int reset;

    if (x86_fb_target() == &state->Target) (void)x86_fb_target_redirect(&state->Target, 0);

    state->Presenting = 0;
    state->Redraw = 1;
    state->Phase = 0;
    os_damage_clear(&state->Damage);
    state->RecoverElapsed = 0;
    if (!state->Io.Clock(state->Io.Context, &state->RecoverLast)) state->Recoverable = 0;
    ++state->Failures;
    reset = reset_private(0);
    return kernel_log(2, reason) && reset;
}

// ====== x86_vgpu_present_bringup ======

// After the transport and the native queue: negotiation, queue, display info, resource, scanout
// and the target. Returns the failure reason, 0 when presenting.
const char *x86_vgpu_present_bringup(void)
{
    KERNEL_VGPU_NEGOTIATION result = {0};
    X86_SURFACE_MAPPING *surface = state->SurfaceSaved;

    state->Native = state->NativeSaved;
    state->Queue = (VGPU_QUEUE){0};
    state->Resource = (VGPU_RESOURCE){0};

    if (!kernel_vgpu_negotiate(&state->Io, &result) || !kernel_vgpu_queue_start(&state->Queue, &state->AccessSaved, &state->BuffersSaved) ||
        !kernel_vgpu_queue_exchange(&state->Queue)
    )
    return "KERNEL_VGPU_PRESENT_START_FAILED";

    state->ResetDone = 0;

    // Geometry A: the resource takes the scanout size from GET_DISPLAY_INFO (VirtIO 1.2 5.7.6.8),
    // packed B8G8R8X8 (stride = width, the pixel layer's format 1); it must fit the surface, which
    // the session sized for X86_VGPU_SURFACE_WIDTH x HEIGHT before the display was known.
    BOOT_U64 needed = kernel_vgpu_surface_bytes(state->Queue.Width, state->Queue.Height);

    if (!needed || needed > surface->Authority.Bytes) return "KERNEL_VGPU_PRESENT_GEOMETRY";

    if (!kernel_vgpu_resource_open(&state->Queue, &state->Resource, surface->Authority.Data.Base, state->BackingSaved, state->Queue.Width, state->Queue.Height) ||
        !kernel_vgpu_scanout_bind(&state->Queue, &state->Resource)
    )
    return "KERNEL_VGPU_PRESENT_SCANOUT_FAILED";

    state->Bound = surface;
    state->Target = (X86_FB_TARGET){(BOOT_U64)&state->Target, X86_SURFACE_VIRTUAL, surface->Authority.Bytes,
        state->Queue.Width, state->Queue.Height, state->Queue.Width, 0, target_private};

    if (!x86_fb_target_redirect(0, &state->Target)) return "KERNEL_VGPU_PRESENT_TARGET_FAILED";

    state->Presenting = 1;
    state->Redraw = 1;
    state->Phase = 0;
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

    if (state->Presenting || state->Transport.Self) return 0;

    volatile unsigned char *bytes = x86_dma_arena_bytes(arena);

    if (!bytes || !x86_vgpu_transport_open(gpu, directory, &state->Transport, &state->Io)) return kernel_log(2, "KERNEL_VGPU_PRESENT_OPEN_FAILED");

    state->Io.Platform = 1;
    buffers.Bits = gpu->Authority.PhysicalBits;

    for (uint32_t index = 0; index < 4; ++index) {
        buffers.Views[index] = bytes + 4096ULL * index;
        buffers.Physical[index] = arena->Authority.Data.Base + 4096ULL * index;
        buffers.Device[index] = x86_vgpu_iommu_device(buffers.Physical[index], 4096, 3);
    }

    BOOT_U64 backing = x86_vgpu_iommu_device(surface->Authority.Data.Base, surface->Authority.Bytes, 1);

    if (!x86_vgpu_queue_open(&state->Transport, &state->Native, &access) ||
        !buffers.Device[0] || !buffers.Device[1] || !buffers.Device[2] || !buffers.Device[3] || !backing
    )
    return x86_vgpu_present_fallback("KERNEL_VGPU_PRESENT_START_FAILED");

    state->NativeSaved = state->Native;
    state->AccessSaved = access;
    state->BuffersSaved = buffers;
    state->BackingSaved = backing;
    state->SurfaceSaved = surface;

    const char *failure = x86_vgpu_present_bringup();

    if (failure) return x86_vgpu_present_fallback(failure);

    return kernel_text("KERNEL_VGPU_PRESENT_SURFACE: ") && kernel_number(surface->Authority.Data.Base) && kernel_text("\n") &&
        kernel_text("KERNEL_VGPU_PRESENT_GEOMETRY: ") && kernel_number(state->Queue.Width) && kernel_text("x") &&
        kernel_number(state->Queue.Height) && kernel_text("\n") && kernel_log(0, "KERNEL_VGPU_PRESENTING");
}

// ====== x86_vgpu_present_abandon ======

int x86_vgpu_present_abandon(void)
{
    return state->Presenting ? x86_vgpu_present_fallback("KERNEL_VGPU_PRESENT_ABANDONED") : 1;
}

// ====== x86_vgpu_present_redraw ======

int x86_vgpu_present_redraw(void)
{
    BOOT_U32 pending = state->Redraw;

    state->Redraw = 0;
    return (int)pending;
}

// ====== x86_vgpu_present_close ======

// Exit: scanout off, detach and unref, then the confirmed reset (VirtIO 1.2 5.7.6.8 order).
int x86_vgpu_present_close(void)
{
    int closed = 1;

    if (!kernel_text("KERNEL_VGPU_PRESENTS: ") || !kernel_number(state->Presents) || !kernel_text("\nKERNEL_VGPU_PRESENT_FAILURES: ") ||
        !kernel_number(state->Failures) || !kernel_text("\n") ||
        !kernel_latency_report("KERNEL_VGPU_PRESENT_PM", &state->PresentTicks) ||
        !kernel_latency_report("KERNEL_VGPU_FLUSH_WAIT_PM", &state->FlushTicks) ||
        !kernel_latency_report("KERNEL_VGPU_PRESENT_AREA", &state->PresentArea) ||
        !kernel_text("KERNEL_VGPU_PRESENT_SLOWS: ") || !kernel_number(state->Slows) || !kernel_text("\nKERNEL_VGPU_PRESENT_TIMEOUTS: ") ||
        !kernel_number(state->Timeouts) || !kernel_text("\nKERNEL_VGPU_PRESENT_RECOVERIES: ") || !kernel_number(state->Recovered) || !kernel_text("\n")
    )
    return 0;

    // The update in flight completes (bounded) before the scanout is disabled.
    for (BOOT_U32 sample = 0; state->Presenting && state->Phase && sample < state->Io.Samples; ++sample) {
        int result = kernel_vgpu_queue_poll(&state->Queue, PRESENT_LIMIT_PM);

        if (result < 0) { (void)x86_vgpu_present_fault(); break; }
        if (result) {
            state->Phase = 0;
            if (!kernel_vgpu_scanout_settle(&state->Queue, &state->Resource)) { (void)x86_vgpu_present_fault(); break; }
        }
    }

    state->Recoverable = 0;

    if (state->Presenting) {
        if (x86_fb_target() == &state->Target) (void)x86_fb_target_redirect(&state->Target, 0);

        closed = kernel_vgpu_scanout_unbind(&state->Queue, &state->Resource) && kernel_vgpu_resource_close(&state->Queue, &state->Resource);
        state->Presenting = 0;
    }

    if (!reset_private(1)) return 0;

    state->Transport = (X86_VGPU_TRANSPORT){0};
    state->ResetDone = 0;
    state->Bound = 0;
    return closed || kernel_log(2, "KERNEL_VGPU_PRESENT_CLOSE_INCOMPLETE");
}
