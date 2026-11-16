#include "runtime.h"
#include "../integration/kernel.h"
#include "arch/x86/kernel/scheduler/integration/control.h"
#include "kernel/diagnostics/log.h"
#include "kernel/sync/internal.h"
#include "../drawing/draw.h"
#include "profile.h"
#include "../pixels/session.h"
#include "kernel/time/clock.h"
#ifdef KERNEL_TEST_PAGING_TAMPER
#include "arch/x86/mm/heap/binding.h"
#endif
_Static_assert(X86_FB_CELL_WIDTH == KERNEL_CONSOLE_CELL_WIDTH && X86_FB_CELL_HEIGHT == KERNEL_CONSOLE_CELL_HEIGHT,
    "Console cell size must match the framebuffer font cell");
// G5 V1-A: one idle audit per second at most.
#include "../live/group.h"

#define AUDIT_PERIOD (KERNEL_PM_TIMER_HZ)

// ====== damage_private ======

// The rectangle is clipped to the view; an empty or invalid one adds nothing.
static void damage_private(X86_CONSOLE *self, uint32_t left, uint32_t top, uint32_t right, uint32_t bottom)
{
    OS_RECT rect = {left, top, right > left ? right - left : 0, bottom > top ? bottom - top : 0}, clipped;

    if (os_rect_clip(&rect, self->View.Columns * KERNEL_CONSOLE_CELL_WIDTH, self->View.Rows * KERNEL_CONSOLE_CELL_HEIGHT, &clipped))
        (void)os_damage_add(&self->Damage, &clipped);
}

// ====== retarget_private ======

// After a target change every cell is drawn again; when the new target is smaller (GOP fallback,
// geometry A) the view is clipped first, keeping the prompt row, and the pointer is clamped.
static int retarget_private(X86_CONSOLE *self)
{
    const OS_FRAMEBUFFER *fb = &self->Source->Env.Context->Info.Framebuffer;
    const X86_FB_TARGET *target = x86_fb_target();
    uint32_t columns = (target ? target->Width : fb->Width) / KERNEL_CONSOLE_CELL_WIDTH;
    uint32_t rows = (target ? target->Height : fb->Height) / KERNEL_CONSOLE_CELL_HEIGHT;

    if (columns < self->View.Columns || rows < self->View.Rows) {
        if (columns > self->View.Columns) columns = self->View.Columns;
        if (rows > self->View.Rows) rows = self->View.Rows;
        if (!kernel_console_service_clip(&self->Service, columns, rows)) return 0;
#ifdef KERNEL_PS2_MOUSE
        if (!kernel_pointer_clamp(&self->Pointer)) return 0;
#endif
        if (!kernel_text("KERNEL_CONSOLE_GRID: ") || !kernel_number(self->View.Columns) || !kernel_text("x") ||
            !kernel_number(self->View.Rows) || !kernel_text("\n")) return 0;
    }

    for (uint32_t index = 0; index < self->View.Columns * self->View.Rows; ++index)
        self->View.Dirty[index / 8] |= (uint8_t)(1U << (index % 8));
    os_damage_clear(&self->Damage);
    self->Render.Next = 0;
    return 1;
}

// ====== console_private ======

// G6: every entry point reaches the console object through the input session that owns it.
static X86_CONSOLE *console_private(X86_PS2_SESSION *session)
{
    return session ? session->Console : 0;
}

// ====== task_valid ======

static int task_valid(const X86_CONSOLE *self, X86_PS2_SESSION *session, uint32_t index)
{
    return self && self->Source && session == self->Source && session == x86_ps2_session() && session->Active &&
        !session->Error && !session->Losses && x86_scheduler_driver_task_valid() && x86_scheduler_index() == index;
}

// ====== wake ======

static int wake(void)
{
#ifdef KERNEL_PS2_MOUSE
    return x86_scheduler_event_request(X86_TASK_SIGNAL, 6, 0).reply.status == KERNEL_SCHEDULER_OK;
#else
    return x86_scheduler_event_request(X86_TASK_SIGNAL, 4, 0).reply.status == KERNEL_SCHEDULER_OK;
#endif
}

// ====== output ======

static int output(void *context, const char *data, uint32_t length)
{
    X86_CONSOLE *self = context;

    return self && task_valid(self, self->Source, 2) &&
        x86_console_requests_send(self, KCR_WRITE, data, length, length);
}

// ====== clock_read ======

static uint32_t clock_read(void *context)
{
    const X86_CONSOLE *self = context;

    if (!self || !task_valid(self, self->Source, 1)) kernel_scheduler_fail_stop();
    return x86_pm_read((unsigned short)self->Source->Clock.Port) & self->Source->Clock.Mask;
}

// ====== elapsed ======

// G4: the counter arithmetic of kernel/time (mask from the session clock, 24 or 32 bit).
static uint32_t elapsed(X86_CONSOLE *self, uint32_t start)
{
    return kernel_counter_delta(self->Source->Clock.Mask, start, clock_read(self));
}

// ====== draw_group ======

static int draw_group(void *context, uint32_t column, uint32_t row,
    const uint32_t *scalars, uint32_t count, uint64_t *ticks)
{
    X86_CONSOLE *self = context;

    if (!self || !self->View.Columns || !self->View.Rows) return 0;
    const KERNEL_VIDEO_CONSOLE *view = &self->View;
    uint32_t caret = view->Caret != UINT32_MAX && view->Caret / view->Columns == row &&
        view->Caret % view->Columns >= column && view->Caret % view->Columns - column < count ?
        view->Caret % view->Columns - column : UINT32_MAX;
    if (!task_valid(self, self->Source, 1) || self->Service.Busy != 2) return 0;
    if (self->Display.Ops) {
        damage_private(self, column * KERNEL_CONSOLE_CELL_WIDTH, row * KERNEL_CONSOLE_CELL_HEIGHT,
            (column + count) * KERNEL_CONSOLE_CELL_WIDTH, (row + 1) * KERNEL_CONSOLE_CELL_HEIGHT);
#ifdef KERNEL_PS2_MOUSE
        // The pointer layer composes the whole 18x18 image and restores the previous box beyond the
        // drawn cells: both boxes belong to the damage.
        const KERNEL_POINTER_IMAGE *image = &self->Pointer.Image;

        if (self->PointerSeen)
            damage_private(self, self->PointerX, self->PointerY, self->PointerX + KERNEL_POINTER_SIZE, self->PointerY + KERNEL_POINTER_SIZE);
        damage_private(self, image->X, image->Y, image->X + KERNEL_POINTER_SIZE, image->Y + KERNEL_POINTER_SIZE);
        self->PointerX = image->X; self->PointerY = image->Y; self->PointerSeen = 1;
#endif
    }
    X86_PROFILE_BEGIN(group_start);
    int drawn;
#ifdef KERNEL_PS2_MOUSE
    if (!kernel_pointer_valid(&self->Pointer)) return 0;
    drawn = x86_fb_kernel_group_pointer(column, row, scalars, count, 0xffffff, 0x081018, ticks, caret, &self->Pointer.Image);
#else
    drawn = x86_fb_kernel_group_caret(column, row, scalars, count, 0xffffff, 0x081018, ticks, caret);
#endif
    X86_PROFILE_END(X86_PROFILE_GROUP, group_start);
    return drawn;
}

// ====== draw_cell ======

static int draw_cell(void *context, uint32_t column, uint32_t row, uint32_t scalar, uint64_t *ticks)
{
    return draw_group(context, column, row, &scalar, 1, ticks);
}

// ====== x86_console_open ======

#ifdef KERNEL_PS2_MOUSE
// ====== pointer_lock ======

static uint64_t pointer_lock(X86_CONSOLE *self, KERNEL_LOCK_GUARD *guard)
{
    uint64_t saved_if = ks_irq_save();
    KERNEL_LOCK_CONTEXT *context = kernel_lock_current_context();
    if (!context || !kernel_lock_acquire(&self->PointerLock, context, guard)) kernel_scheduler_fail_stop();
    return saved_if;
}

// ====== pointer_unlock ======

static void pointer_unlock(KERNEL_LOCK_GUARD *guard, uint64_t saved_if)
{
    if (!kernel_lock_release(kernel_lock_current_context(), guard)) kernel_scheduler_fail_stop();
    ks_irq_restore(saved_if);
}

// ====== saturate_private ======

static int32_t saturate_private(int32_t total, int32_t delta)
{
    int64_t sum = (int64_t)total + delta;
    return sum > INT32_MAX ? INT32_MAX : sum < INT32_MIN ? INT32_MIN : (int32_t)sum;
}
#endif

// ====== x86_console_open ======

int x86_console_open(X86_PS2_SESSION *session, KERNEL_DISPLAY display)
{
    X86_CONSOLE *self = console_private(session);
    const OS_FRAMEBUFFER *fb;
    if (!self || self->Source || session != x86_ps2_session() || !session->Env.Context ||
        !session->Clock.Ready || !session->Clock.Port || session->Clock.Port > 65535 ||
        (session->Clock.Mask != 0xffffff && session->Clock.Mask != UINT32_MAX) ||
        (display.Ops && (!display.Ops->Check || !display.Ops->Poll || !display.Ops->Busy ||
            !display.Ops->Redraw || !display.Ops->Rect))) return 0;
    self->Source = session;
    self->Display = display;
    self->PointerFrame = UINT64_MAX;
    self->PointerVisibility = 2;
    if (!os_damage_init(&self->Damage, 1)) return 0;
    x86_fb_verify_reset();
#ifdef KERNEL_PROFILE_VGPU_FLUSH
    x86_profile_start(session->Clock.Port, session->Clock.Mask);
#endif
    fb = &session->Env.Context->Info.Framebuffer;
    if (!fb->Base) { self->Absent = 1; return kernel_log(1, "KERNEL_CONSOLE_SHELL_ABSENT"); }
#ifdef KERNEL_PS2_MOUSE
    if (!kernel_lock_initialize(&self->PointerLock, X86_CONSOLE_POINTER_RANK)) return 0;
#endif
    // Geometry A: the grid follows the active drawing target (the VirtIO surface when presenting).
    const X86_FB_TARGET *target = x86_fb_target();
    uint32_t columns = (target ? target->Width : fb->Width) / KERNEL_CONSOLE_CELL_WIDTH;
    uint32_t rows = (target ? target->Height : fb->Height) / KERNEL_CONSOLE_CELL_HEIGHT;
    if (columns > KERNEL_CONSOLE_COLUMNS) columns = KERNEL_CONSOLE_COLUMNS;
    if (rows > KERNEL_CONSOLE_ROWS) rows = KERNEL_CONSOLE_ROWS;
    if (!kernel_console_requests_initialize(&self->Requests) ||
        !kernel_console_service_init(&self->Service, &self->View, columns, rows) ||
        !kernel_text("KERNEL_CONSOLE_GRID: ") || !kernel_number(columns) || !kernel_text("x") ||
        !kernel_number(rows) || !kernel_text("\n")) return 0;
#ifdef KERNEL_PS2_MOUSE
    if (!kernel_pointer_init(&self->Pointer, &self->View) || !kernel_log(0, "KERNEL_POINTER_OPEN")) return 0;
#endif
    return kernel_console_render_init(&self->Render, &self->View, draw_cell, self) &&
        kernel_console_batch_init(&self->Batch, &self->Render, draw_group, self) &&
        kernel_console_measure_init(&self->Measured, &self->Render, clock_read, self, session->Clock.Mask) &&
        kernel_ps2_shell_output_bind(output, self) && kernel_log(0, "KERNEL_CONSOLE_SHELL_OPEN");
}

// ====== x86_console_mouse ======

int x86_console_mouse(X86_PS2_SESSION *session, int32_t dx, int32_t dy, uint32_t buttons)
{
#ifdef KERNEL_PS2_MOUSE
    X86_CONSOLE *self = console_private(session);
    if (!task_valid(self, session, 3)) return 0;
    if (self->Absent || session->Stop) return 1;
    KERNEL_LOCK_GUARD guard = {0};
    uint64_t saved_if = pointer_lock(self, &guard);
    int accepted = 0, moved = 0;
    if (!self->Service.Closed && !self->Service.Failed) {
        if (self->Service.Busy) {
            self->DeferredX = saturate_private(self->DeferredX, dx);
            self->DeferredY = saturate_private(self->DeferredY, dy);
            self->DeferredButtons = buttons;
            self->Deferred = 1;
            accepted = 1;
        } else {
            uint64_t before = self->Pointer.Moves;
            accepted = kernel_pointer_request(&self->Pointer, dx, dy, buttons);
            moved = accepted && self->Pointer.Moves != before;
            if (moved) {
                self->PointerPending = 1;
                self->Render.Next = self->Pointer.Image.Y / KERNEL_CONSOLE_CELL_HEIGHT * self->View.Columns +
                    self->Pointer.Image.X / KERNEL_CONSOLE_CELL_WIDTH;
            }
        }
    }
    pointer_unlock(&guard, saved_if);
    if (!accepted) return 0;
    return moved ? wake() : 1;
#else
    (void)session; (void)dx; (void)dy; (void)buttons;
    return 0;
#endif
}

#ifdef KERNEL_PS2_MOUSE
// ====== x86_console_pointer_deferred ======

int x86_console_pointer_deferred(X86_CONSOLE *self)
{
    if (!self || self->Absent) return 1;
    KERNEL_LOCK_GUARD guard = {0};
    uint64_t saved_if = pointer_lock(self, &guard);
    int accepted = 1;
    if (self->Deferred && !self->Service.Busy) {
        uint64_t before = self->Pointer.Moves;
        accepted = kernel_pointer_request(&self->Pointer, self->DeferredX, self->DeferredY, self->DeferredButtons);
        if (accepted && self->Pointer.Moves != before) self->PointerPending = 1;
        self->DeferredX = self->DeferredY = 0;
        self->Deferred = 0;
    }
    pointer_unlock(&guard, saved_if);
    return accepted;
}
#endif

// ====== x86_console_edit ======

int x86_console_edit(X86_PS2_SESSION *session)
{
    X86_CONSOLE *self = console_private(session);
    if (!task_valid(self, session, 0)) return 0;
    if (self->Absent || session->Stop) return 1;
    return x86_console_requests_send(self, KCR_EDIT, session->Line.Data, session->Line.Length,
        sizeof(session->Line.Data));
}

// ====== x86_console_submit ======

int x86_console_submit(X86_PS2_SESSION *session)
{
    X86_CONSOLE *self = console_private(session);
    if (!task_valid(self, session, 0)) return 0;
    if (self->Absent || session->Stop) return 1;
    return x86_console_requests_send(self, KCR_SUBMIT, NULL, 0, 0);
}

// ====== x86_console_pending ======

int x86_console_pending(X86_PS2_SESSION *session)
{
    X86_CONSOLE *self = console_private(session);
    if (!task_valid(self, session, 1)) return -1;
#ifdef KERNEL_PS2_MOUSE
    if (!self->Absent && session->Quiet) {
        KERNEL_LOCK_GUARD guard = {0};
        uint64_t saved_if = pointer_lock(self, &guard);
        int hidden = kernel_pointer_hide(&self->Pointer);
        pointer_unlock(&guard, saved_if);
        if (!hidden) return -1;
    }
#endif
    if (self->Absent) return 0;
    int requests = x86_console_requests_pending(self);
    if (requests != 0) return requests;
    int pending = kernel_console_service_pending(&self->Service);
    if (pending == 0 && self->Display.Ops && self->Display.Ops->Busy(self->Display.Self)) return 1;
#ifdef KERNEL_PS2_MOUSE
    if (pending >= 0 && (self->PointerPending || self->Deferred)) return 1;
#endif
    return pending;
}

// ====== flush_failure ======

static int flush_failure(const X86_CONSOLE *self, uint32_t stage)
{
    kernel_text("CONSOLE_FAILURE_STAGE: "); kernel_number(stage);
    if (self) {
        kernel_text("\nCONSOLE_FAILURE_PM: "); kernel_number(self->Measured.LastTicks);
        kernel_text("\nCONSOLE_FAILURE_FLAGS: ");
        kernel_number((uint64_t)self->Measured.Failed | (uint64_t)self->Batch.Failed << 8 | (uint64_t)self->Render.Busy << 16 |
            (uint64_t)self->Service.Failed << 24 | (uint64_t)self->Service.Busy << 32);
        kernel_text("\nCONSOLE_FAILURE_CELLS: "); kernel_number(self->Measured.Cells);
        kernel_text("\nCONSOLE_FAILURE_RENDERED: "); kernel_number(self->Render.CellsDone);
    }
    kernel_text("\n");
    return 0;
}

// ====== pointer_settled ======

#ifdef KERNEL_PS2_MOUSE
static int pointer_settled(X86_CONSOLE *self)
{
    const KERNEL_CONSOLE_POINTER *pointer = &self->Pointer;
    if (self->PointerFrame == pointer->Moves && self->PointerVisibility == pointer->Image.Visible &&
        self->PointerCells == self->Render.CellsDone) return 1;
    int pending = kernel_console_service_pending(&self->Service);
    if (pending < 0) return 0;
    if (pending) return 1;
#ifndef KERNEL_INPUT_INTERACTIVE
    if (!kernel_text("KERNEL_POINTER_FRAME_X: ") || !kernel_number(pointer->Image.X) ||
        !kernel_text("\nKERNEL_POINTER_FRAME_Y: ") || !kernel_number(pointer->Image.Y) ||
        !kernel_text("\nKERNEL_POINTER_FRAME_VISIBLE: ") || !kernel_number(pointer->Image.Visible) ||
        !kernel_text("\nKERNEL_POINTER_FRAME_MOVES: ") || !kernel_number(pointer->Moves) ||
        !kernel_text("\nKERNEL_POINTER_FRAME_CELLS: ") || !kernel_number(self->Render.CellsDone) ||
        !kernel_text("\n") || !kernel_log(0, "KERNEL_POINTER_FRAME")) return 0;
#else
    if (self->PointerFrame == UINT64_MAX && !kernel_log(0,"KERNEL_POINTER_READY")) return 0;
#endif
    self->PointerFrame = pointer->Moves; self->PointerVisibility = pointer->Image.Visible;
    self->PointerCells = self->Render.CellsDone;
    return 1;
}
#endif

// ====== x86_console_audit ======

// Called by the console worker when it has nothing to draw: one full audit of the paging
// inventory per second at most. 0 only when the audit failed (the caller stops the session).
int x86_console_audit(X86_PS2_SESSION *session)
{
    X86_CONSOLE *self = console_private(session);
    if (!task_valid(self, session, 1)) return 0;
    if (self->Absent || session->Stop) return 1;

#ifdef KERNEL_TEST_PAGING_TAMPER
    // Test-only: after 200 flushes one heap PTE is changed outside the APIs; the next audit stops.
    if (self->FlushCount >= 200 && !self->Tampered) {
        self->Tampered = 1;
        if (!x86_heap_tamper()) return kernel_log(3, "KERNEL_PAGING_TAMPER_FAILED") && 0;
    }
#endif
    uint32_t now = clock_read(self);

    if (self->AuditStarted && kernel_counter_delta(session->Clock.Mask, self->AuditLast, now) < AUDIT_PERIOD) return 1;

    int audited = x86_fb_kernel_audit();
    if (audited == X86_FB_AUDIT_RETRY) return 1;
    self->AuditStarted = 1;
    self->AuditLast = now;
    uint32_t spent = elapsed(self, now);

    ++self->AuditCount;
    self->AuditTotal += spent;
    if (spent > self->AuditMax) self->AuditMax = spent;
    if (audited == X86_FB_AUDIT_OK) return 1;

    (void)kernel_log(3, "KERNEL_PAGING_AUDIT_FAILED");
    return 0;
}

// ====== x86_console_flush ======

int x86_console_flush(X86_PS2_SESSION *session)
{
    X86_CONSOLE *self = console_private(session);
    const KERNEL_DISPLAY *display = self ? &self->Display : 0;
    uint32_t result, begin, spent;
    if (!task_valid(self, session, 1)) return flush_failure(self, 1);
    uint64_t overruns = self->Measured.Overruns;
    X86_PROFILE_BEGIN(flush_start);
    // V2b Y3-A: the display's fault scan rides on every console flush (registers only).
    X86_PROFILE_BEGIN(poll_start);
    if (display->Ops && !display->Ops->Check(display->Self)) return flush_failure(self, 1);
    X86_PROFILE_END(X86_PROFILE_POLL, poll_start);
    if (self->Absent) return 1;
    if (!x86_console_requests_step(self)) return flush_failure(self, 8);
    // R1-A: the update in flight advances (or a recovery runs) before anything is drawn. After a
    // target change (bring-up or GOP fallback) every cell is drawn again on the new target.
    if (display->Ops && (!display->Ops->Poll(display->Self) ||
        (display->Ops->Redraw(display->Self) && !retarget_private(self)))) return flush_failure(self, 7);
    begin = clock_read(self);
#ifdef KERNEL_PS2_MOUSE
    if (!x86_console_pointer_deferred(self)) return flush_failure(self, 6);
    KERNEL_LOCK_GUARD guard = {0};
    uint64_t saved_if = pointer_lock(self, &guard);
    if (self->PointerPending) {
        uint32_t index = self->Pointer.Image.Y / KERNEL_CONSOLE_CELL_HEIGHT * self->View.Columns +
            self->Pointer.Image.X / KERNEL_CONSOLE_CELL_WIDTH;
        self->View.Dirty[index / 8] |= (uint8_t)(1U << (index % 8));
        self->Render.Next = index;
        self->PointerPending = 0;
    }
    int began = kernel_console_service_render_begin(&self->Service);
    pointer_unlock(&guard, saved_if);
    if (!began) return flush_failure(self, 2);
#else
    if (!kernel_console_service_render_begin(&self->Service)) return flush_failure(self, 2);
#endif
    X86_PROFILE_BEGIN(render_start);
    result = kernel_console_measure_batch_step_live(&self->Measured, &self->Batch);
    X86_PROFILE_END(X86_PROFILE_RENDER, render_start);
#ifdef KERNEL_PS2_MOUSE
    saved_if = pointer_lock(self, &guard);
    int ended = kernel_console_service_render_end(&self->Service, result != 0);
    pointer_unlock(&guard, saved_if);
    if (!ended) return flush_failure(self, 3);
#else
    if (!kernel_console_service_render_end(&self->Service, result != 0)) return flush_failure(self, 3);
#endif
    if (!result || self->Measured.Failed || self->Batch.Failed || self->Render.Busy) return flush_failure(self, 4);
#ifdef KERNEL_PS2_MOUSE
    {
        uint32_t mark = clock_read(self);
        saved_if = pointer_lock(self, &guard);
        int settled = pointer_settled(self);
        pointer_unlock(&guard, saved_if);
        self->SettleTotal += elapsed(self, mark);
        if (!settled) return flush_failure(self, 6);
    }
#endif
    // Z2-A: every flush that drew something presents its bounding rectangle once.
    if (display->Ops && !os_damage_empty(&self->Damage)) {
        OS_RECT box;
        (void)os_damage_bounds(&self->Damage, &box);
        os_damage_clear(&self->Damage);
        X86_PROFILE_BEGIN(present_start);
        if (!display->Ops->Rect(display->Self, box.X, box.Y, box.Width, box.Height)) return flush_failure(self, 7);
        X86_PROFILE_END(X86_PROFILE_PRESENT, present_start);
        if (display->Ops->Redraw(display->Self) && (!retarget_private(self) || !wake())) return flush_failure(self, 7);
    }
    X86_PROFILE_END(X86_PROFILE_FLUSH, flush_start);
    spent = elapsed(self, begin);
    ++self->FlushCount; self->FlushTotal += spent;
    if (result == KERNEL_CONSOLE_RENDER_IDLE) { ++self->IdleCount; self->IdleTotal += spent; }
    if (spent > self->FlushMax) self->FlushMax = spent;
    uint64_t now = self->Measured.Overruns;
    if (now != overruns && !(now & (now - 1))) {
        if (!kernel_text("CONSOLE_SLOW_GROUP_PM: ") || !kernel_number(self->Measured.LastTicks) ||
            !kernel_text("\n") || !kernel_log(2, "KERNEL_CONSOLE_SLOW_GROUP")) return flush_failure(self, 5);
    }
    return 1;
}

// ====== x86_console_finish ======

int x86_console_finish(X86_PS2_SESSION *session)
{
    X86_CONSOLE *self = console_private(session);
#ifdef KERNEL_PS2_MOUSE
    if (!session || !session->Mouse.Done || session->Mouse.Token.Id || session->Mouse.RouteOwned ||
        session->Mouse.Decoder.State || session->Mouse.Buttons) return 0;
#endif
    if (!self || !self->Source || session != self->Source || session->Active || !session->Quiet ||
        !session->WorkerDone || !session->ConsumerDone || !session->ShellDone || session->Error || session->Losses)
        return 0;
    if (self->Absent) return kernel_log(0, "KERNEL_CONSOLE_SHELL_SERIAL_CLEAN");
    BOOT_U64 verify_full = 0, verify_skipped = 0, verify_accepted = 0;
    x86_fb_verify_counts(&verify_full, &verify_skipped, &verify_accepted);
#ifdef KERNEL_PS2_MOUSE
    if (!kernel_pointer_close(&self->Pointer) || !kernel_text("KERNEL_POINTER_X: ") ||
        !kernel_number(self->Pointer.Image.X) || !kernel_text("\nKERNEL_POINTER_Y: ") ||
        !kernel_number(self->Pointer.Image.Y) || !kernel_text("\nKERNEL_POINTER_MOVES: ") ||
        !kernel_number(self->Pointer.Moves) || !kernel_text("\n") || !kernel_log(0, "KERNEL_POINTER_CLEAN")) return 0;
#endif
    const KERNEL_CONSOLE_MEASUREMENT *measured = &self->Measured;
    if (!self->RequestsClosed || kernel_console_service_pending(&self->Service) != 0 || measured->Failed || self->Batch.Failed ||
        !measured->Cells || self->Render.CellsDone != measured->Cells || !self->Batch.Groups ||
        !kernel_ps2_shell_output_unbind(output, self) || !kernel_console_service_close(&self->Service)) return 0;
    return kernel_log(0, "KERNEL_CONSOLE_REQUESTS_CLEAN") &&
        kernel_text("KERNEL_CONSOLE_SHELL_CELLS: ") && kernel_number(measured->Cells) &&
        kernel_text("\nKERNEL_CONSOLE_SHELL_GROUPS: ") && kernel_number(self->Batch.Groups) &&
        kernel_text("\nKERNEL_CONSOLE_SHELL_MAX_PM: ") && kernel_number(measured->MaxTicks) &&
        kernel_text("\nKERNEL_CONSOLE_SHELL_OVERRUNS: ") && kernel_number(measured->Overruns) &&
        kernel_text("\nKERNEL_CONSOLE_FLUSHES: ") && kernel_number(self->FlushCount) &&
        kernel_text("\nKERNEL_CONSOLE_FLUSH_TOTAL_PM: ") && kernel_number(self->FlushTotal) &&
        kernel_text("\nKERNEL_CONSOLE_FLUSH_MAX_PM: ") && kernel_number(self->FlushMax) &&
        kernel_text("\nKERNEL_CONSOLE_IDLE_FLUSHES: ") && kernel_number(self->IdleCount) &&
        kernel_text("\nKERNEL_CONSOLE_IDLE_TOTAL_PM: ") && kernel_number(self->IdleTotal) &&
        kernel_text("\nKERNEL_CONSOLE_SETTLE_TOTAL_PM: ") && kernel_number(self->SettleTotal) &&
        kernel_text("\nKERNEL_FB_VERIFY_FULL: ") && kernel_number(verify_full) &&
        kernel_text("\nKERNEL_FB_VERIFY_SKIPPED: ") && kernel_number(verify_skipped) &&
        kernel_text("\nKERNEL_FB_VERIFY_ACCEPTED: ") && kernel_number(verify_accepted) &&
        kernel_text("\nKERNEL_PAGING_AUDITS: ") && kernel_number(self->AuditCount) &&
        kernel_text("\nKERNEL_PAGING_AUDIT_TOTAL_PM: ") && kernel_number(self->AuditTotal) &&
        kernel_text("\nKERNEL_PAGING_AUDIT_MAX_PM: ") && kernel_number(self->AuditMax) &&
        kernel_text("\n") &&
#ifdef KERNEL_PROFILE_VGPU_FLUSH
        x86_profile_report() &&
#endif
        kernel_log(0, "KERNEL_CONSOLE_SHELL_CLEAN");
}
