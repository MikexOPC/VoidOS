#include "state.h"

// ====== x86_vgpu_present_fault ======

// A failed post or completion: report, then the Z4-A fallback. Only a timeout may be recovered.
int x86_vgpu_present_fault(X86_VGPU_PRESENT *self)
{
    self->Recoverable = self->Queue.Fault == VGPU_QUEUE_TIME;
    if (self->Recoverable) ++self->Timeouts;

    (void)(kernel_text("KERNEL_VGPU_PRESENT_RECT: ") &&
        kernel_number(((BOOT_U64)self->FlyX << 48) | ((BOOT_U64)self->FlyY << 32) | ((BOOT_U64)self->FlyW << 16) | self->FlyH) &&
        kernel_text("\nKERNEL_VGPU_PRESENT_QUEUE_FAULT: ") && kernel_number(self->Queue.Fault) && kernel_text("\nKERNEL_VGPU_PRESENT_PHASE: ") &&
        kernel_number(self->Phase) && kernel_text("\n"));
    return x86_vgpu_present_fallback(self, self->Recoverable ? "KERNEL_VGPU_PRESENT_TIMEOUT" : "KERNEL_VGPU_PRESENT_FALLBACK");
}

// ====== elapsed_private ======

static BOOT_U32 elapsed_private(X86_VGPU_PRESENT *self)
{
    return kernel_clock_elapsed(&self->Clock, self->SubmittedAt);
}

// ====== complete_private ======

// R1-A: observe the command in flight (phase 1 or 2) once. 1 completed, 0 pending, -1 fault.
static int complete_private(X86_VGPU_PRESENT *self)
{
    int result;

#ifdef KERNEL_TEST_VGPU_PRESENT_STALL
    BOOT_U32 stall = self->Presents == KERNEL_TEST_VGPU_PRESENT_STALL ? 4295454U :
        self->Presents == KERNEL_TEST_VGPU_PRESENT_STALL + 200 ? 715909U : 0;

    result = self->StallHold ? 1 : kernel_vgpu_queue_poll(&self->Queue, PRESENT_LIMIT_PM);

    if (result == 1 && self->Phase == 1 && stall) {
        BOOT_U32 elapsed = elapsed_private(self);

        self->StallHold = elapsed < stall;
        if (self->StallHold && elapsed >= PRESENT_LIMIT_PM) { self->Queue.Fault = VGPU_QUEUE_TIME; self->StallHold = 0; return -1; }
        if (self->StallHold) result = 0;
    }
#else
    result = kernel_vgpu_queue_poll(&self->Queue, PRESENT_LIMIT_PM);
#endif

    if (result) return result;

    // R2: past 100 ms the update is marked slow once; the 1 s limit is enforced by the queue poll.
    if (!self->SlowMarked && elapsed_private(self) >= PRESENT_SLOW_PM) {
        self->SlowMarked = 1;
        ++self->Slows;
        (void)kernel_log(2, "KERNEL_VGPU_PRESENT_SLOW");
    }

    return 0;
}

// ====== x86_vgpu_present_pump ======

// Advances the update in flight and starts the next one from the merged damage. Never waits.
int x86_vgpu_present_pump(X86_VGPU_PRESENT *self)
{
    for (BOOT_U32 step = 0; self->Presenting && step < 4; ++step) {
        if (!self->Phase) {
            if (os_damage_empty(&self->Damage)) return 1;

            ++self->Presents;

#ifdef KERNEL_TEST_VGPU_PRESENT_FAULT
            // Test-only: the N-th present behaves as a failed command to exercise the fallback path.
            if (self->Presents == KERNEL_TEST_VGPU_PRESENT_FAULT) { self->Recoverable = 0; return x86_vgpu_present_fallback(self, "KERNEL_VGPU_PRESENT_FALLBACK"); }
#endif

            OS_RECT box;

            (void)os_damage_bounds(&self->Damage, &box);
            self->FlyX = box.X; self->FlyY = box.Y; self->FlyW = box.Width; self->FlyH = box.Height;
            os_damage_clear(&self->Damage);

            if (!kernel_clock_now(&self->Clock, &self->SubmittedAt) ||
                !kernel_vgpu_scanout_post(&self->Queue, &self->Resource, 0, self->FlyX, self->FlyY, self->FlyW, self->FlyH)
            )
            return x86_vgpu_present_fault(self);

            self->Phase = 1;
            self->SlowMarked = 0;
            continue;
        }

        int result = complete_private(self);

        if (result < 0) return x86_vgpu_present_fault(self);

        if (!result) return 1;

        if (self->Phase == 1) {
            if (!kernel_vgpu_scanout_post(&self->Queue, &self->Resource, 1, self->FlyX, self->FlyY, self->FlyW, self->FlyH)) return x86_vgpu_present_fault(self);

            self->Phase = 2;
            continue;
        }

        if (!kernel_vgpu_scanout_settle(&self->Queue, &self->Resource)) return x86_vgpu_present_fault(self);

        kernel_latency_add(&self->PresentTicks, elapsed_private(self));
        kernel_latency_add(&self->FlushTicks, self->Queue.LastWait);
        kernel_latency_add(&self->PresentArea, (BOOT_U64)self->FlyW * self->FlyH);
        self->Phase = 0;
    }

    return 1;
}

// ====== recover_private ======

// R3-A: after a timeout fallback, at most 3 bring-ups per session, 5 s apart. The surface is
// cleared and presented whole; the console redraws its (possibly clipped) grid on it.
static int recover_private(X86_VGPU_PRESENT *self)
{
    if (self->Presenting || !self->Recoverable || self->Recoveries >= RECOVER_TRIES || !self->Transport.Self) return 1;

    if (!kernel_stopwatch_sample(&self->Clock, &self->Recover) || self->Recover.Total < RECOVER_SPACING_PM) return 1;

    ++self->Recoveries;
    self->Recoverable = 0;

    const char *failure = x86_vgpu_present_bringup(self);

    if (failure) return x86_vgpu_present_fallback(self, failure);

    // Not x86_vgpu_surface_pixels: it recaptures the paging inventory, which the session tasks must
    // not do (the console accounting keeps its own baseline). The authority check is enough here.
    if (!x86_vgpu_surface_check(&self->SurfaceSaved->Authority)) return x86_vgpu_present_fallback(self, "KERNEL_VGPU_PRESENT_RECOVERY_FAILED");

    volatile uint32_t *pixels = self->Pixels;

    for (BOOT_U64 index = 0; index < (BOOT_U64)self->Queue.Width * self->Queue.Height; ++index) pixels[index] = 0;

    (void)os_damage_add(&self->Damage, &(OS_RECT){0, 0, self->Queue.Width, self->Queue.Height});
    ++self->Recovered;
    return kernel_log(1, "KERNEL_VGPU_PRESENT_RECOVERED") && x86_vgpu_present_pump(self);
}

// ====== x86_vgpu_present_rect ======

// Z2-A with R1-A: the dirty rectangle joins the queued damage; TRANSFER_TO_HOST_2D and
// RESOURCE_FLUSH are posted and observed on later calls, never waited for.
int x86_vgpu_present_rect(X86_VGPU_PRESENT *self, BOOT_U32 x, BOOT_U32 y, BOOT_U32 width, BOOT_U32 height)
{
    if (!self->Presenting || !width || !height) return self->Presenting ? x86_vgpu_present_pump(self) : 1;

    if (!os_damage_add(&self->Damage, &(OS_RECT){x, y, width, height})) return 1;

    return x86_vgpu_present_pump(self);
}

// ====== x86_vgpu_present_poll ======

// Every console flush: progress of the update in flight, or the recovery after a timeout.
int x86_vgpu_present_poll(X86_VGPU_PRESENT *self) { return self->Presenting ? x86_vgpu_present_pump(self) : recover_private(self); }

// ====== x86_vgpu_present_busy ======

// The console keeps flushing while an update is in flight, damage waits or a recovery is due.
int x86_vgpu_present_busy(X86_VGPU_PRESENT *self)
{
    return self->Presenting ? self->Phase || !os_damage_empty(&self->Damage) : self->Recoverable && self->Recoveries < RECOVER_TRIES && self->Transport.Self;
}
