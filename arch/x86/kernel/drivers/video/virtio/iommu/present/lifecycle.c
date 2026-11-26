#include "state.h"

// ====== target_private ======

// The console target stays valid only while the surface is the registered, published one. The
// target is embedded in its present object, so the object is found from the target itself.
static int target_private(const X86_FB_TARGET *candidate)
{
    if (!candidate || candidate->Self != (BOOT_U64)candidate) return 0;

    const X86_VGPU_PRESENT *self = (const X86_VGPU_PRESENT *)((const char *)candidate - offsetof(X86_VGPU_PRESENT, Target));

    return self->Bound && x86_surface_owner() == &self->Bound->Authority &&
        x86_vgpu_surface_check(&self->Bound->Authority);
}

// ====== reset_private ======

// VirtIO 1.2 2.4: status 0 resets the device; confirmed before anything else is released.
// The transport is ended (which recaptures the paging inventory) only outside the session tasks:
// inside them the live stacks are mapped and the console accounting keeps its own baseline.
static int reset_private(X86_VGPU_PRESENT *self, int final)
{
    KERNEL_VGPU_NEGOTIATION result = {0};

    self->Queue.State = 0;
    self->Native.Self = 0;

    if (!self->Transport.Self) return 1;

    if (!self->ResetDone && !kernel_vgpu_reset(&self->Io, &result)) return 0;

    self->ResetDone = 1;
    return !final || x86_vgpu_transport_end(&self->Transport);
}

// ====== x86_vgpu_present_fallback ======

// Z4-A: first error → stop presenting, reset the GPU, back to GOP with a full console redraw.
int x86_vgpu_present_fallback(X86_VGPU_PRESENT *self, const char *reason)
{
    int reset;

    if (x86_fb_target() == &self->Target) (void)x86_fb_target_redirect(&self->Target, 0);

    self->Presenting = 0;
    self->Redraw = 1;
    self->Phase = 0;
    os_damage_clear(&self->Damage);
    if (!kernel_stopwatch_start(&self->Clock, &self->Recover)) self->Recoverable = 0;
    ++self->Failures;
    reset = reset_private(self, 0);
    return kernel_log(2, reason) && reset;
}

// ====== x86_vgpu_present_bringup ======

// After the transport and the native queue: negotiation, queue, display info, resource, scanout
// and the target. Returns the failure reason, 0 when presenting.
const char *x86_vgpu_present_bringup(X86_VGPU_PRESENT *self)
{
    KERNEL_VGPU_NEGOTIATION result = {0};
    X86_SURFACE_MAPPING *surface = self->SurfaceSaved;

    self->Native = self->NativeSaved;
    self->Queue = (VGPU_QUEUE){0};
    self->Resource = (VGPU_RESOURCE){0};

    if (!kernel_vgpu_negotiate(&self->Io, &result) || !kernel_vgpu_queue_start(&self->Queue, &self->AccessSaved, &self->BuffersSaved) ||
        !kernel_vgpu_queue_exchange(&self->Queue)
    )
    return "KERNEL_VGPU_PRESENT_START_FAILED";

    self->ResetDone = 0;

    // Geometry A: the resource takes the scanout size from GET_DISPLAY_INFO (VirtIO 1.2 5.7.6.8),
    // packed B8G8R8X8 (stride = width, the pixel layer's format 1); it must fit the surface, which
    // the session sized for X86_VGPU_SURFACE_WIDTH x HEIGHT before the display was known.
    BOOT_U64 needed = kernel_vgpu_surface_bytes(self->Queue.Width, self->Queue.Height);

    if (!needed || needed > surface->Authority.Bytes) return "KERNEL_VGPU_PRESENT_GEOMETRY";

    if (!kernel_vgpu_resource_open(&self->Queue, &self->Resource, surface->Authority.Data.Base, self->BackingSaved, self->Queue.Width, self->Queue.Height) ||
        !kernel_vgpu_scanout_bind(&self->Queue, &self->Resource)
    )
    return "KERNEL_VGPU_PRESENT_SCANOUT_FAILED";

    self->Bound = surface;
    self->Target = (X86_FB_TARGET){(BOOT_U64)&self->Target, X86_SURFACE_VIRTUAL, surface->Authority.Bytes,
        self->Queue.Width, self->Queue.Height, self->Queue.Width, 0, target_private};

    if (!x86_fb_target_redirect(0, &self->Target)) return "KERNEL_VGPU_PRESENT_TARGET_FAILED";

    self->Presenting = 1;
    self->Redraw = 1;
    self->Phase = 0;
    return 0;
}

// ====== x86_vgpu_present_open ======

// Z5-A: inside the V2b start, after TE=1 and before the watchdog. Returns 0 only when the GPU
// reset could not be confirmed; a refused or failed bring-up leaves the console on GOP.
int x86_vgpu_present_open(X86_VGPU_PRESENT *self, X86_VGPU_MAPPING *gpu, const KERNEL_ACPI_DIRECTORY *directory,
    X86_DMA_ARENA *arena, X86_SURFACE_MAPPING *surface)
{
    VGPU_QUEUE_IO_CONTEXT access = {0};
    VGPU_BUFFERS buffers = {0};

    if (self->Presenting || self->Transport.Self || !os_damage_init(&self->Damage, 1)) return 0;

    volatile unsigned char *bytes = x86_dma_arena_bytes(arena);

    if (!bytes || !x86_vgpu_transport_open(gpu, directory, &self->Transport, &self->Io)) return kernel_log(2, "KERNEL_VGPU_PRESENT_OPEN_FAILED");

    self->Io.Platform = self->DirectDma ? 0U : 1U;
    self->Clock = (KERNEL_CLOCK){self->Io.Clock, self->Io.Context, self->Io.Mask, KERNEL_PM_TIMER_HZ};
    buffers.Bits = gpu->Authority.PhysicalBits;

    for (uint32_t index = 0; index < 4; ++index) {
        buffers.Views[index] = bytes + 4096ULL * index;
        buffers.Physical[index] = arena->Authority.Data.Base + 4096ULL * index;
#ifdef KERNEL_VGPU_IOMMU
        buffers.Device[index] = self->DirectDma
            ? buffers.Physical[index]
            : x86_vgpu_iommu_device(buffers.Physical[index], 4096, 3);
#else
        buffers.Device[index] = buffers.Physical[index];
#endif
    }

#ifdef KERNEL_VGPU_IOMMU
    BOOT_U64 backing = self->DirectDma
        ? surface->Authority.Data.Base
        : x86_vgpu_iommu_device(surface->Authority.Data.Base, surface->Authority.Bytes, 1);
#else
    BOOT_U64 backing = surface->Authority.Data.Base;
#endif

    if (!x86_vgpu_queue_open(&self->Transport, &self->Native, &access) ||
        !buffers.Device[0] || !buffers.Device[1] || !buffers.Device[2] || !buffers.Device[3] || !backing
    )
    return x86_vgpu_present_fallback(self, "KERNEL_VGPU_PRESENT_START_FAILED");

    self->NativeSaved = self->Native;
    self->AccessSaved = access;
    self->BuffersSaved = buffers;
    self->BackingSaved = backing;
    self->SurfaceSaved = surface;
    self->Pixels = (volatile uint32_t *)X86_SURFACE_VIRTUAL;

    const char *failure = x86_vgpu_present_bringup(self);

    if (failure) return x86_vgpu_present_fallback(self, failure);

    return kernel_text("KERNEL_VGPU_PRESENT_SURFACE: ") && kernel_number(surface->Authority.Data.Base) && kernel_text("\n") &&
        kernel_text("KERNEL_VGPU_PRESENT_GEOMETRY: ") && kernel_number(self->Queue.Width) && kernel_text("x") &&
        kernel_number(self->Queue.Height) && kernel_text("\n") && kernel_log(0, "KERNEL_VGPU_PRESENTING");
}

// ====== x86_vgpu_present_abandon ======

int x86_vgpu_present_abandon(X86_VGPU_PRESENT *self)
{
    return self->Presenting ? x86_vgpu_present_fallback(self, "KERNEL_VGPU_PRESENT_ABANDONED") : 1;
}

// ====== x86_vgpu_present_redraw ======

int x86_vgpu_present_redraw(X86_VGPU_PRESENT *self)
{
    BOOT_U32 pending = self->Redraw;

    self->Redraw = 0;
    return (int)pending;
}

// ====== x86_vgpu_present_close ======

// Exit: scanout off, detach and unref, then the confirmed reset (VirtIO 1.2 5.7.6.8 order).
int x86_vgpu_present_close(X86_VGPU_PRESENT *self)
{
    int closed = 1;

    if (!kernel_text("KERNEL_VGPU_PRESENTS: ") || !kernel_number(self->Presents) || !kernel_text("\nKERNEL_VGPU_PRESENT_FAILURES: ") ||
        !kernel_number(self->Failures) || !kernel_text("\n") ||
        !kernel_latency_report("KERNEL_VGPU_PRESENT_PM", &self->PresentTicks) ||
        !kernel_latency_report("KERNEL_VGPU_FLUSH_WAIT_PM", &self->FlushTicks) ||
        !kernel_latency_report("KERNEL_VGPU_PRESENT_AREA", &self->PresentArea) ||
        !kernel_text("KERNEL_VGPU_PRESENT_SLOWS: ") || !kernel_number(self->Slows) || !kernel_text("\nKERNEL_VGPU_PRESENT_TIMEOUTS: ") ||
        !kernel_number(self->Timeouts) || !kernel_text("\nKERNEL_VGPU_PRESENT_RECOVERIES: ") || !kernel_number(self->Recovered) || !kernel_text("\n")
    )
    return 0;

    // The update in flight completes (bounded) before the scanout is disabled.
    for (BOOT_U32 sample = 0; self->Presenting && self->Phase && sample < self->Io.Samples; ++sample) {
        int result = kernel_vgpu_queue_poll(&self->Queue, PRESENT_LIMIT_PM);

        if (result < 0) { (void)x86_vgpu_present_fault(self); break; }
        if (result) {
            self->Phase = 0;
            if (!kernel_vgpu_scanout_settle(&self->Queue, &self->Resource)) { (void)x86_vgpu_present_fault(self); break; }
        }
    }

    self->Recoverable = 0;

    if (self->Presenting) {
        if (x86_fb_target() == &self->Target) (void)x86_fb_target_redirect(&self->Target, 0);

        closed = kernel_vgpu_scanout_unbind(&self->Queue, &self->Resource) && kernel_vgpu_resource_close(&self->Queue, &self->Resource);
        self->Presenting = 0;
    }

    if (!reset_private(self, 1)) return 0;

    self->Transport = (X86_VGPU_TRANSPORT){0};
    self->ResetDone = 0;
    self->Bound = 0;
    return closed || kernel_log(2, "KERNEL_VGPU_PRESENT_CLOSE_INCOMPLETE");
}
