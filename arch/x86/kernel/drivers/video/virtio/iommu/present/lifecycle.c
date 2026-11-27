#include "state.h"
#include "kernel/console/video/pointer/shape.h"

// ====== cursor_word_private ======

static void cursor_word_private(unsigned char *body, uint32_t at, uint32_t value)
{
    for (uint32_t i = 0U; i < 4U; ++i)
        body[at + i] = (unsigned char)(value >> (i * 8U));
}

// ====== cursor_init_private ======

// VirtIO 1.2 Section 5.7.6.10: 64x64 B8G8R8A8_UNORM (format=1) cursor resource id=2.
static void cursor_init_private(X86_VGPU_PRESENT *self)
{
    self->CursorReady = 0U;
    self->CursorVisible = 0U;
    if (!self->CursorPixels || !self->CursorDevice)
        return;
    for (uint32_t i = 0U; i < 64U * 64U; ++i)
        self->CursorPixels[i] = 0U;
    for (uint32_t row = 0U; row < KERNEL_POINTER_SIZE; ++row) {
        for (uint32_t col = 0U; col < KERNEL_POINTER_SIZE; ++col) {
            uint32_t sample = kernel_pointer_sample(col, row);
            if (sample)
                self->CursorPixels[row * 64U + col] = (kernel_pointer_blend(sample, 0U) & 0x00FFFFFFU) | 0xFF000000U;
        }
    }
    unsigned char create_body[16] = {0};
    cursor_word_private(create_body, 0U, 2U);
    cursor_word_private(create_body, 4U, 1U);
    cursor_word_private(create_body, 8U, 64U);
    cursor_word_private(create_body, 12U, 64U);
    if (!kernel_vgpu_queue_control(&self->Queue, 0x0101U, create_body, 16U))
        return;

    unsigned char attach_body[24] = {0};
    cursor_word_private(attach_body, 0U, 2U);
    cursor_word_private(attach_body, 4U, 1U);
    cursor_word_private(attach_body, 8U, (uint32_t)self->CursorDevice);
    cursor_word_private(attach_body, 12U, (uint32_t)(self->CursorDevice >> 32));
    cursor_word_private(attach_body, 16U, 64U * 64U * 4U);
    if (!kernel_vgpu_queue_control(&self->Queue, 0x0106U, attach_body, 24U))
        return;

    unsigned char xfer_body[32] = {0};
    cursor_word_private(xfer_body, 8U, 64U);
    cursor_word_private(xfer_body, 12U, 64U);
    cursor_word_private(xfer_body, 24U, 2U);
    if (!kernel_vgpu_queue_control(&self->Queue, 0x0105U, xfer_body, 32U))
        return;

    (void)kernel_vgpu_cursor_post(&self->Queue, 0x0300U, 0U, 0U, 0U,
        KERNEL_POINTER_HOTSPOT_X, KERNEL_POINTER_HOTSPOT_Y);
    self->CursorReady = 1U;
    (void)kernel_log(0, "KERNEL_VGPU_CURSOR_READY");
}

// ====== x86_vgpu_present_cursor ======

int x86_vgpu_present_cursor(X86_VGPU_PRESENT *self, BOOT_U32 x, BOOT_U32 y, BOOT_U32 visible)
{
    if (!self || !self->Presenting || !self->CursorReady)
        return 0;
    uint32_t vis = visible ? 1U : 0U;
    uint32_t cmd = (vis != self->CursorVisible) ? 0x0300U : 0x0301U;
    uint32_t res_id = vis ? 2U : 0U;
    if (!kernel_vgpu_cursor_post(&self->Queue, cmd, x, y, res_id,
        KERNEL_POINTER_HOTSPOT_X, KERNEL_POINTER_HOTSPOT_Y))
        return 0;
    self->CursorVisible = vis;
    return 1;
}

// ====== x86_vgpu_present_check_resize ======

// VirtIO 1.2 Section 5.7.4: device config region 3, offset 0x00 events_read (bit 0 = VIRTIO_GPU_EVENT_DISPLAY),
// offset 0x04 events_clear (W1C). When the host window resizes, re-query GET_DISPLAY_INFO (5.7.6.8) and
// rebind scanout 0 on the pre-allocated 1920x1080 backing surface if the new geometry fits.
int x86_vgpu_present_check_resize(X86_VGPU_PRESENT *self)
{
    if (!self || !self->Presenting || self->Phase || !os_damage_empty(&self->Damage) || !self->SurfaceSaved)
        return 1;
    uint32_t events = 0U;
    if (!x86_vgpu_queue_register(&self->Native, 3U, 0U, 4U, 0U, &events))
        return 1;
    if ((events & 1U) == 0U)
        return 1;

    uint32_t clear_bit = 1U;
    (void)x86_vgpu_queue_register(&self->Native, 3U, 4U, 4U, 1U, &clear_bit);

    uint32_t old_w = self->Queue.Width;
    uint32_t old_h = self->Queue.Height;
    uint32_t old_scanout = self->Queue.Scanout;
    if (!kernel_vgpu_queue_exchange(&self->Queue))
        return x86_vgpu_present_fallback(self, "KERNEL_VGPU_RESIZE_INFO_FAILED");

    uint32_t new_w = self->Queue.Width;
    uint32_t new_h = self->Queue.Height;
    uint32_t new_scanout = self->Queue.Scanout;
    if (new_w == old_w && new_h == old_h)
        return 1;

    BOOT_U64 needed = kernel_vgpu_surface_bytes(new_w, new_h);
    if (!needed || needed > self->SurfaceSaved->Authority.Bytes) {
        self->Queue.Width = old_w;
        self->Queue.Height = old_h;
        self->Queue.Scanout = old_scanout;
        return 1;
    }

    self->Queue.Width = old_w;
    self->Queue.Height = old_h;
    self->Queue.Scanout = old_scanout;
    if (!kernel_vgpu_scanout_unbind(&self->Queue, &self->Resource) ||
        !kernel_vgpu_resource_close(&self->Queue, &self->Resource))
        return x86_vgpu_present_fallback(self, "KERNEL_VGPU_RESIZE_UNBIND_FAILED");

    self->Queue.Width = new_w;
    self->Queue.Height = new_h;
    self->Queue.Scanout = new_scanout;
    self->Resource = (VGPU_RESOURCE){0};
    if (!kernel_vgpu_resource_open(&self->Queue, &self->Resource, self->SurfaceSaved->Authority.Data.Base,
            self->BackingSaved, new_w, new_h) ||
        !kernel_vgpu_scanout_bind(&self->Queue, &self->Resource))
        return x86_vgpu_present_fallback(self, "KERNEL_VGPU_RESIZE_REBIND_FAILED");

    self->Target.Width = new_w;
    self->Target.Height = new_h;
    self->Target.Stride = new_w;
    self->Redraw = 1U;
    (void)(kernel_text("KERNEL_VGPU_RESIZE: ") && kernel_number(new_w) &&
        kernel_text("x") && kernel_number(new_h) && kernel_text("\n"));
    return 1;
}

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
    cursor_init_private(self);
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
    if (self->DirectDma && arena->Authority.Data.Pages >= 8U) {
        self->CursorPixels = (volatile uint32_t *)(bytes + 4ULL * 4096ULL);
        self->CursorDevice = arena->Authority.Data.Base + 4ULL * 4096ULL;
    } else {
        self->CursorPixels = 0;
        self->CursorDevice = 0;
    }

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

        if (self->CursorReady) {
            unsigned char cur_id[8] = {0};
            cursor_word_private(cur_id, 0U, 2U);
            (void)kernel_vgpu_cursor_post(&self->Queue, 0x0300U, 0U, 0U, 0U, 0U, 0U);
            (void)kernel_vgpu_queue_control(&self->Queue, 0x0107U, cur_id, 8U);
            (void)kernel_vgpu_queue_control(&self->Queue, 0x0102U, cur_id, 8U);
            self->CursorReady = 0U;
            self->CursorVisible = 0U;
        }
        closed = kernel_vgpu_scanout_unbind(&self->Queue, &self->Resource) && kernel_vgpu_resource_close(&self->Queue, &self->Resource);
        self->Presenting = 0;
    }

    if (!reset_private(self, 1)) return 0;

    self->Transport = (X86_VGPU_TRANSPORT){0};
    self->ResetDone = 0;
    self->Bound = 0;
    return closed || kernel_log(2, "KERNEL_VGPU_PRESENT_CLOSE_INCOMPLETE");
}
