#include "state.h"

static X86_VGPU_PRESENT *const state = &x86_vgpu_present_state;

// ====== x86_vgpu_present_fault ======

// A failed post or completion: report, then the Z4-A fallback. Only a timeout may be recovered.
int x86_vgpu_present_fault(void)
{
    state->Recoverable = state->Queue.Fault == VGPU_QUEUE_TIME;
    if (state->Recoverable) ++state->Timeouts;

    (void)(kernel_text("KERNEL_VGPU_PRESENT_RECT: ") &&
        kernel_number(((BOOT_U64)state->FlyX << 48) | ((BOOT_U64)state->FlyY << 32) | ((BOOT_U64)state->FlyW << 16) | state->FlyH) &&
        kernel_text("\nKERNEL_VGPU_PRESENT_QUEUE_FAULT: ") && kernel_number(state->Queue.Fault) && kernel_text("\nKERNEL_VGPU_PRESENT_PHASE: ") &&
        kernel_number(state->Phase) && kernel_text("\n"));
    return x86_vgpu_present_fallback(state->Recoverable ? "KERNEL_VGPU_PRESENT_TIMEOUT" : "KERNEL_VGPU_PRESENT_FALLBACK");
}

// ====== elapsed_private ======

static BOOT_U32 elapsed_private(void)
{
    uint32_t now = 0;

    return state->Io.Clock(state->Io.Context, &now) ? (now - state->SubmittedAt) & state->Io.Mask : PRESENT_LIMIT_PM;
}

// ====== complete_private ======

// R1-A: observe the command in flight (phase 1 or 2) once. 1 completed, 0 pending, -1 fault.
static int complete_private(void)
{
    int result;

#ifdef KERNEL_TEST_VGPU_PRESENT_STALL
    BOOT_U32 stall = state->Presents == KERNEL_TEST_VGPU_PRESENT_STALL ? 4295454U :
        state->Presents == KERNEL_TEST_VGPU_PRESENT_STALL + 200 ? 715909U : 0;

    result = state->StallHold ? 1 : kernel_vgpu_queue_poll(&state->Queue, PRESENT_LIMIT_PM);

    if (result == 1 && state->Phase == 1 && stall) {
        BOOT_U32 elapsed = elapsed_private();

        state->StallHold = elapsed < stall;
        if (state->StallHold && elapsed >= PRESENT_LIMIT_PM) { state->Queue.Fault = VGPU_QUEUE_TIME; state->StallHold = 0; return -1; }
        if (state->StallHold) result = 0;
    }
#else
    result = kernel_vgpu_queue_poll(&state->Queue, PRESENT_LIMIT_PM);
#endif

    if (result) return result;

    // R2: past 100 ms the update is marked slow once; the 1 s limit is enforced by the queue poll.
    if (!state->SlowMarked && elapsed_private() >= PRESENT_SLOW_PM) {
        state->SlowMarked = 1;
        ++state->Slows;
        (void)kernel_log(2, "KERNEL_VGPU_PRESENT_SLOW");
    }

    return 0;
}

// ====== x86_vgpu_present_pump ======

// Advances the update in flight and starts the next one from the merged damage. Never waits.
int x86_vgpu_present_pump(void)
{
    for (BOOT_U32 step = 0; state->Presenting && step < 4; ++step) {
        if (!state->Phase) {
            if (os_damage_empty(&state->Damage)) return 1;

            ++state->Presents;

#ifdef KERNEL_TEST_VGPU_PRESENT_FAULT
            // Test-only: the N-th present behaves as a failed command to exercise the fallback path.
            if (state->Presents == KERNEL_TEST_VGPU_PRESENT_FAULT) { state->Recoverable = 0; return x86_vgpu_present_fallback("KERNEL_VGPU_PRESENT_FALLBACK"); }
#endif

            OS_RECT box;

            (void)os_damage_bounds(&state->Damage, &box);
            state->FlyX = box.X; state->FlyY = box.Y; state->FlyW = box.Width; state->FlyH = box.Height;
            os_damage_clear(&state->Damage);

            if (!state->Io.Clock(state->Io.Context, &state->SubmittedAt) ||
                !kernel_vgpu_scanout_post(&state->Queue, &state->Resource, 0, state->FlyX, state->FlyY, state->FlyW, state->FlyH)
            )
            return x86_vgpu_present_fault();

            state->Phase = 1;
            state->SlowMarked = 0;
            continue;
        }

        int result = complete_private();

        if (result < 0) return x86_vgpu_present_fault();

        if (!result) return 1;

        if (state->Phase == 1) {
            if (!kernel_vgpu_scanout_post(&state->Queue, &state->Resource, 1, state->FlyX, state->FlyY, state->FlyW, state->FlyH)) return x86_vgpu_present_fault();

            state->Phase = 2;
            continue;
        }

        if (!kernel_vgpu_scanout_settle(&state->Queue, &state->Resource)) return x86_vgpu_present_fault();

        kernel_latency_add(&state->PresentTicks, elapsed_private());
        kernel_latency_add(&state->FlushTicks, state->Queue.LastWait);
        kernel_latency_add(&state->PresentArea, (BOOT_U64)state->FlyW * state->FlyH);
        state->Phase = 0;
    }

    return 1;
}

// ====== recover_private ======

// R3-A: after a timeout fallback, at most 3 bring-ups per session, 5 s apart. The surface is
// cleared and presented whole; the console redraws its (possibly clipped) grid on it.
static int recover_private(void)
{
    uint32_t now = 0;

    if (state->Presenting || !state->Recoverable || state->Recoveries >= RECOVER_TRIES || !state->Transport.Self) return 1;

    if (!state->Io.Clock(state->Io.Context, &now)) return 1;

    state->RecoverElapsed += (now - state->RecoverLast) & state->Io.Mask;
    state->RecoverLast = now;
    if (state->RecoverElapsed < RECOVER_SPACING_PM) return 1;

    ++state->Recoveries;
    state->Recoverable = 0;

    const char *failure = x86_vgpu_present_bringup();

    if (failure) return x86_vgpu_present_fallback(failure);

    // Not x86_vgpu_surface_pixels: it recaptures the paging inventory, which the session tasks must
    // not do (the console accounting keeps its own baseline). The authority check is enough here.
    if (!x86_vgpu_surface_check(&state->SurfaceSaved->Authority)) return x86_vgpu_present_fallback("KERNEL_VGPU_PRESENT_RECOVERY_FAILED");

    volatile uint32_t *pixels = (volatile uint32_t *)X86_SURFACE_VIRTUAL;

    for (BOOT_U64 index = 0; index < (BOOT_U64)state->Queue.Width * state->Queue.Height; ++index) pixels[index] = 0;

    (void)os_damage_add(&state->Damage, &(OS_RECT){0, 0, state->Queue.Width, state->Queue.Height});
    ++state->Recovered;
    return kernel_log(1, "KERNEL_VGPU_PRESENT_RECOVERED") && x86_vgpu_present_pump();
}

// ====== x86_vgpu_present_rect ======

// Z2-A with R1-A: the dirty rectangle joins the queued damage; TRANSFER_TO_HOST_2D and
// RESOURCE_FLUSH are posted and observed on later calls, never waited for.
int x86_vgpu_present_rect(BOOT_U32 x, BOOT_U32 y, BOOT_U32 width, BOOT_U32 height)
{
    if (!state->Presenting || !width || !height) return state->Presenting ? x86_vgpu_present_pump() : 1;

    if (!os_damage_add(&state->Damage, &(OS_RECT){x, y, width, height})) return 1;

    return x86_vgpu_present_pump();
}

// ====== x86_vgpu_present_poll ======

// Every console flush: progress of the update in flight, or the recovery after a timeout.
int x86_vgpu_present_poll(void) { return state->Presenting ? x86_vgpu_present_pump() : recover_private(); }

// ====== x86_vgpu_present_busy ======

// The console keeps flushing while an update is in flight, damage waits or a recovery is due.
int x86_vgpu_present_busy(void)
{
    return state->Presenting ? state->Phase || !os_damage_empty(&state->Damage) : state->Recoverable && state->Recoveries < RECOVER_TRIES && state->Transport.Self;
}
