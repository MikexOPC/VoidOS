#include "runtime.h"
#include "../integration/kernel.h"
#include "kernel/console/video/service/console.h"
#include "kernel/console/video/measurement/budget.h"
#include "kernel/console/video/pointer/state.h"
#include "arch/x86/kernel/scheduler/integration/control.h"
#include "kernel/diagnostics/log.h"
#include "../drawing/draw.h"
#include "profile.h"
#include "../pixels/session.h"
#ifdef KERNEL_VGPU_IOMMU
#include "arch/x86/kernel/drivers/video/virtio/iommu/session.h"
#endif
_Static_assert(X86_FB_CELL_WIDTH == KERNEL_CONSOLE_CELL_WIDTH && X86_FB_CELL_HEIGHT == KERNEL_CONSOLE_CELL_HEIGHT,
    "Console cell size must match the framebuffer font cell");
static KERNEL_CONSOLE_SERVICE service;
static KERNEL_VIDEO_CONSOLE view;
static KERNEL_CONSOLE_RENDER render;
static KERNEL_CONSOLE_BATCH batch;
static KERNEL_CONSOLE_MEASUREMENT measured;
static X86_PS2_SESSION *source;
static uint32_t absent;
// Timing of flushes in PM ticks (3.579545 MHz), reported when the session closes. Idle
// flushes found nothing to draw; settle is the time spent in the serial pointer record.
static uint64_t flush_count, flush_total, settle_total, idle_count, idle_total;
static uint32_t flush_max;
#ifdef KERNEL_VGPU_IOMMU
// V2c Z2-A: cells drawn since the last present, as one pixel rectangle (Width 0 = nothing).
static uint32_t damage_x, damage_y, damage_right, damage_bottom;
static uint32_t pointer_x, pointer_y, pointer_seen;

// ====== damage_private ======

static void damage_private(uint32_t left, uint32_t top, uint32_t right, uint32_t bottom)
{
    uint32_t width = view.Columns * KERNEL_CONSOLE_CELL_WIDTH, height = view.Rows * KERNEL_CONSOLE_CELL_HEIGHT;

    if (right > width) right = width;
    if (bottom > height) bottom = height;
    if (left >= right || top >= bottom) return;

    if (damage_right == damage_x) { damage_x = left; damage_y = top; damage_right = right; damage_bottom = bottom; return; }

    if (left < damage_x) damage_x = left;
    if (top < damage_y) damage_y = top;
    if (right > damage_right) damage_right = right;
    if (bottom > damage_bottom) damage_bottom = bottom;
}
#endif
#ifdef KERNEL_PS2_MOUSE
static KERNEL_CONSOLE_POINTER pointer;
static uint64_t pointer_frame = UINT64_MAX;
static uint64_t pointer_cells;
static uint32_t pointer_visibility = 2;
static uint32_t pointer_pending;
#endif

#ifdef KERNEL_VGPU_IOMMU
// ====== retarget_private ======

// After a target change every cell is drawn again; when the new target is smaller (GOP fallback,
// geometry A) the view is clipped first, keeping the prompt row, and the pointer is clamped.
static int retarget_private(void)
{
    const OS_FRAMEBUFFER *fb = &source->Env.Context->Info.Framebuffer;
    const X86_FB_TARGET *target = x86_fb_target();
    uint32_t columns = (target ? target->Width : fb->Width) / KERNEL_CONSOLE_CELL_WIDTH;
    uint32_t rows = (target ? target->Height : fb->Height) / KERNEL_CONSOLE_CELL_HEIGHT;

    if (columns < view.Columns || rows < view.Rows) {
        if (columns > view.Columns) columns = view.Columns;
        if (rows > view.Rows) rows = view.Rows;
        if (!kernel_console_service_clip(&service, columns, rows)) return 0;
#ifdef KERNEL_PS2_MOUSE
        if (!kernel_pointer_clamp(&pointer)) return 0;
#endif
        if (!kernel_text("KERNEL_CONSOLE_GRID: ") || !kernel_number(view.Columns) || !kernel_text("x") ||
            !kernel_number(view.Rows) || !kernel_text("\n")) return 0;
    }

    for (uint32_t index = 0; index < view.Columns * view.Rows; ++index) view.Dirty[index / 8] |= (uint8_t)(1U << (index % 8));
    damage_x = damage_y = damage_right = damage_bottom = 0;
    render.Next = 0;
    return 1;
}
#endif

// ====== task_valid ======

static int task_valid(X86_PS2_SESSION *session, uint32_t index)
{
    return source && session == source && session == x86_ps2_session() && session->Active &&
        !session->Error && !session->Losses && x86_scheduler_task_valid() && x86_scheduler_index() == index;
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
    return context == source && task_valid(source, 2) &&
        kernel_console_service_write(&service, data, length, length) && wake();
}

// ====== clock_read ======

static uint32_t clock_read(void *context)
{
    if (context != source || !task_valid(source, 1)) kernel_scheduler_fail_stop();
    return x86_pm_read((unsigned short)source->Clock.Port) & source->Clock.Mask;
}

// ====== elapsed ======

static uint32_t elapsed(uint32_t start)
{
    return (clock_read(source) - start) & source->Clock.Mask;
}

// ====== draw_group ======

static int draw_group(void *context, uint32_t column, uint32_t row,
    const uint32_t *scalars, uint32_t count, uint64_t *ticks)
{
    if (!view.Columns || !view.Rows) return 0;
    uint32_t caret = view.Caret != UINT32_MAX && view.Caret / view.Columns == row &&
        view.Caret % view.Columns >= column && view.Caret % view.Columns - column < count ?
        view.Caret % view.Columns - column : UINT32_MAX;
    if (context != source || !task_valid(source, 1) || service.Busy != 2) return 0;
#ifdef KERNEL_VGPU_IOMMU
    damage_private(column * KERNEL_CONSOLE_CELL_WIDTH, row * KERNEL_CONSOLE_CELL_HEIGHT,
        (column + count) * KERNEL_CONSOLE_CELL_WIDTH, (row + 1) * KERNEL_CONSOLE_CELL_HEIGHT);
#ifdef KERNEL_PS2_MOUSE
    // The pointer layer composes the whole 18x18 image and restores the previous box beyond the
    // drawn cells: both boxes belong to the damage.
    if (pointer_seen) damage_private(pointer_x, pointer_y, pointer_x + KERNEL_POINTER_SIZE, pointer_y + KERNEL_POINTER_SIZE);
    damage_private(pointer.Image.X, pointer.Image.Y, pointer.Image.X + KERNEL_POINTER_SIZE, pointer.Image.Y + KERNEL_POINTER_SIZE);
    pointer_x = pointer.Image.X; pointer_y = pointer.Image.Y; pointer_seen = 1;
#endif
#endif
    X86_PROFILE_BEGIN(group_start);
    int drawn;
#ifdef KERNEL_PS2_MOUSE
    if (!kernel_pointer_valid(&pointer)) return 0;
    drawn = x86_fb_kernel_group_pointer(column, row, scalars, count, 0xffffff, 0x081018, ticks, caret, &pointer.Image);
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

int x86_console_open(X86_PS2_SESSION *session)
{
    const OS_FRAMEBUFFER *fb;
    if (source || !session || session != x86_ps2_session() || !session->Env.Context ||
        !session->Clock.Ready || !session->Clock.Port || session->Clock.Port > 65535 ||
        (session->Clock.Mask != 0xffffff && session->Clock.Mask != UINT32_MAX)) return 0;
    source = session;
    x86_fb_verify_reset();
#ifdef KERNEL_PROFILE_VGPU_FLUSH
    x86_profile_start(session->Clock.Port, session->Clock.Mask);
#endif
    fb = &session->Env.Context->Info.Framebuffer;
    if (!fb->Base) { absent = 1; return kernel_log(1, "KERNEL_CONSOLE_SHELL_ABSENT"); }
    // Geometry A: the grid follows the active drawing target (the VirtIO surface when presenting).
    const X86_FB_TARGET *target = x86_fb_target();
    uint32_t columns = (target ? target->Width : fb->Width) / KERNEL_CONSOLE_CELL_WIDTH;
    uint32_t rows = (target ? target->Height : fb->Height) / KERNEL_CONSOLE_CELL_HEIGHT;
    if (columns > KERNEL_CONSOLE_COLUMNS) columns = KERNEL_CONSOLE_COLUMNS;
    if (rows > KERNEL_CONSOLE_ROWS) rows = KERNEL_CONSOLE_ROWS;
    if (!kernel_console_service_init(&service, &view, columns, rows) ||
        !kernel_text("KERNEL_CONSOLE_GRID: ") || !kernel_number(columns) || !kernel_text("x") ||
        !kernel_number(rows) || !kernel_text("\n")) return 0;
#ifdef KERNEL_PS2_MOUSE
    if (!kernel_pointer_init(&pointer, &view) || !kernel_log(0, "KERNEL_POINTER_OPEN")) return 0;
#endif
    return kernel_console_render_init(&render, &view, draw_cell, source) &&
        kernel_console_batch_init(&batch, &render, draw_group, source) &&
        kernel_console_measure_init(&measured, &render, clock_read, source, source->Clock.Mask) &&
        kernel_ps2_shell_output_bind(output, source) && kernel_log(0, "KERNEL_CONSOLE_SHELL_OPEN");
}

// ====== x86_console_mouse ======

int x86_console_mouse(X86_PS2_SESSION *session, int32_t dx, int32_t dy, uint32_t buttons)
{
#ifdef KERNEL_PS2_MOUSE
    if (!task_valid(session, 3)) return 0;
    if (absent || session->Stop) return 1;
    uint64_t before = pointer.Moves;
    if (service.Busy || service.Closed || service.Failed ||
        !kernel_pointer_request(&pointer, dx, dy, buttons)) return 0;
    if (pointer.Moves == before) return 1;
    pointer_pending = 1;
    render.Next = pointer.Image.Y / KERNEL_CONSOLE_CELL_HEIGHT * view.Columns + pointer.Image.X / KERNEL_CONSOLE_CELL_WIDTH;
    return wake();
#else
    (void)session; (void)dx; (void)dy; (void)buttons;
    return 0;
#endif
}

// ====== x86_console_edit ======

int x86_console_edit(X86_PS2_SESSION *session)
{
    if (!task_valid(session, 0)) return 0;
    if (absent || session->Stop) return 1;
    return kernel_console_service_edit(&service, session->Line.Data, session->Line.Length,
        sizeof(session->Line.Data)) && wake();
}

// ====== x86_console_submit ======

int x86_console_submit(X86_PS2_SESSION *session)
{
    if (!task_valid(session, 0)) return 0;
    if (absent || session->Stop) return 1;
    return kernel_console_service_submit(&service) && wake();
}

// ====== x86_console_pending ======

int x86_console_pending(X86_PS2_SESSION *session)
{
    if (!task_valid(session, 1)) return -1;
#ifdef KERNEL_PS2_MOUSE
    if (!absent && session->Quiet && !kernel_pointer_hide(&pointer)) return -1;
#endif
    if (absent) return 0;
    int pending = kernel_console_service_pending(&service);
#ifdef KERNEL_PS2_MOUSE
    if (pending >= 0 && pointer_pending) return 1;
#endif
    return pending;
}

// ====== flush_failure ======

static int flush_failure(uint32_t stage)
{
    kernel_text("CONSOLE_FAILURE_STAGE: "); kernel_number(stage);
    kernel_text("\nCONSOLE_FAILURE_PM: "); kernel_number(measured.LastTicks);
    kernel_text("\nCONSOLE_FAILURE_FLAGS: ");
    kernel_number((uint64_t)measured.Failed | (uint64_t)batch.Failed << 8 |
        (uint64_t)render.Busy << 16 | (uint64_t)service.Failed << 24 | (uint64_t)service.Busy << 32);
    kernel_text("\nCONSOLE_FAILURE_CELLS: "); kernel_number(measured.Cells);
    kernel_text("\nCONSOLE_FAILURE_RENDERED: "); kernel_number(render.CellsDone);
    kernel_text("\n");
    return 0;
}

// ====== pointer_settled ======

#ifdef KERNEL_PS2_MOUSE
static int pointer_settled(void)
{
    if (pointer_frame == pointer.Moves && pointer_visibility == pointer.Image.Visible && pointer_cells == render.CellsDone) return 1;
    int pending = kernel_console_service_pending(&service);
    if (pending < 0) return 0;
    if (pending) return 1;
#ifndef KERNEL_INPUT_INTERACTIVE
    if (!kernel_text("KERNEL_POINTER_FRAME_X: ") || !kernel_number(pointer.Image.X) ||
        !kernel_text("\nKERNEL_POINTER_FRAME_Y: ") || !kernel_number(pointer.Image.Y) ||
        !kernel_text("\nKERNEL_POINTER_FRAME_VISIBLE: ") || !kernel_number(pointer.Image.Visible) ||
        !kernel_text("\nKERNEL_POINTER_FRAME_MOVES: ") || !kernel_number(pointer.Moves) ||
        !kernel_text("\nKERNEL_POINTER_FRAME_CELLS: ") || !kernel_number(render.CellsDone) ||
        !kernel_text("\n") || !kernel_log(0, "KERNEL_POINTER_FRAME")) return 0;
#else
    if (pointer_frame == UINT64_MAX && !kernel_log(0,"KERNEL_POINTER_READY")) return 0;
#endif
    pointer_frame = pointer.Moves; pointer_visibility = pointer.Image.Visible;
    pointer_cells = render.CellsDone;
    return 1;
}
#endif

// ====== x86_console_flush ======

int x86_console_flush(X86_PS2_SESSION *session)
{
    uint32_t result, begin, spent;
    uint64_t overruns = measured.Overruns;
    if (!task_valid(session, 1)) return flush_failure(1);
    X86_PROFILE_BEGIN(flush_start);
#ifdef KERNEL_VGPU_IOMMU
    // V2b Y3-A: the GPU fault scan rides on every console flush (registers only).
    X86_PROFILE_BEGIN(poll_start);
    if (!x86_vgpu_session_poll()) return flush_failure(1);
    X86_PROFILE_END(X86_PROFILE_POLL, poll_start);
#endif
    if (absent) return 1;
#ifdef KERNEL_VGPU_IOMMU
    // After a target change (bring-up or GOP fallback) every cell is drawn again on the new target.
    if (x86_vgpu_present_redraw() && !retarget_private()) return flush_failure(7);
#endif
    begin = clock_read(source);
#ifdef KERNEL_PS2_MOUSE
    if (pointer_pending) {
        uint32_t index = pointer.Image.Y / KERNEL_CONSOLE_CELL_HEIGHT * view.Columns + pointer.Image.X / KERNEL_CONSOLE_CELL_WIDTH;
        view.Dirty[index / 8] |= (uint8_t)(1U << (index % 8));
        render.Next = index;
        pointer_pending = 0;
    }
#endif
    if (!kernel_console_service_render_begin(&service)) return flush_failure(2);
    X86_PROFILE_BEGIN(render_start);
    result = kernel_console_measure_batch_step_live(&measured, &batch);
    X86_PROFILE_END(X86_PROFILE_RENDER, render_start);
    if (!kernel_console_service_render_end(&service, result != 0)) return flush_failure(3);
    if (!result || measured.Failed || batch.Failed || render.Busy) return flush_failure(4);
#ifdef KERNEL_PS2_MOUSE
    {
        uint32_t mark = clock_read(source);
        int settled = pointer_settled();
        settle_total += elapsed(mark);
        if (!settled) return flush_failure(6);
    }
#endif
#ifdef KERNEL_VGPU_IOMMU
    // Z2-A: every flush that drew something presents its bounding rectangle once.
    if (damage_right != damage_x) {
        uint32_t x = damage_x, y = damage_y, w = damage_right - damage_x, h = damage_bottom - damage_y;
        damage_x = damage_y = damage_right = damage_bottom = 0;
        X86_PROFILE_BEGIN(present_start);
        if (!x86_vgpu_present_rect(x, y, w, h)) return flush_failure(7);
        X86_PROFILE_END(X86_PROFILE_PRESENT, present_start);
        if (x86_vgpu_present_redraw() && (!retarget_private() || !wake())) return flush_failure(7);
    }
#endif
    X86_PROFILE_END(X86_PROFILE_FLUSH, flush_start);
    spent = elapsed(begin);
    ++flush_count; flush_total += spent;
    if (result == KERNEL_CONSOLE_RENDER_IDLE) { ++idle_count; idle_total += spent; }
    if (spent > flush_max) flush_max = spent;
    if (measured.Overruns != overruns && !(measured.Overruns & (measured.Overruns - 1))) {
        if (!kernel_text("CONSOLE_SLOW_GROUP_PM: ") || !kernel_number(measured.LastTicks) ||
            !kernel_text("\n") || !kernel_log(2, "KERNEL_CONSOLE_SLOW_GROUP")) return flush_failure(5);
    }
    return 1;
}

// ====== x86_console_finish ======

int x86_console_finish(X86_PS2_SESSION *session)
{
#ifdef KERNEL_PS2_MOUSE
    if (!session || !session->Mouse.Done || session->Mouse.Token.Id || session->Mouse.RouteOwned ||
        session->Mouse.Decoder.State || session->Mouse.Buttons) return 0;
#endif
    if (!source || session != source || session->Active || !session->Quiet ||
        !session->WorkerDone || !session->ConsumerDone || !session->ShellDone || session->Error || session->Losses)
        return 0;
    if (absent) return kernel_log(0, "KERNEL_CONSOLE_SHELL_SERIAL_CLEAN");
    BOOT_U64 verify_full = 0, verify_skipped = 0;
    x86_fb_verify_counts(&verify_full, &verify_skipped);
#ifdef KERNEL_PS2_MOUSE
    if (!kernel_pointer_close(&pointer) || !kernel_text("KERNEL_POINTER_X: ") ||
        !kernel_number(pointer.Image.X) || !kernel_text("\nKERNEL_POINTER_Y: ") ||
        !kernel_number(pointer.Image.Y) || !kernel_text("\nKERNEL_POINTER_MOVES: ") ||
        !kernel_number(pointer.Moves) || !kernel_text("\n") || !kernel_log(0, "KERNEL_POINTER_CLEAN")) return 0;
#endif
    if (kernel_console_service_pending(&service) != 0 || measured.Failed || batch.Failed ||
        !measured.Cells || render.CellsDone != measured.Cells || !batch.Groups ||
        !kernel_ps2_shell_output_unbind(output, source) || !kernel_console_service_close(&service)) return 0;
    return kernel_text("KERNEL_CONSOLE_SHELL_CELLS: ") && kernel_number(measured.Cells) &&
        kernel_text("\nKERNEL_CONSOLE_SHELL_GROUPS: ") && kernel_number(batch.Groups) &&
        kernel_text("\nKERNEL_CONSOLE_SHELL_MAX_PM: ") && kernel_number(measured.MaxTicks) &&
        kernel_text("\nKERNEL_CONSOLE_SHELL_OVERRUNS: ") && kernel_number(measured.Overruns) &&
        kernel_text("\nKERNEL_CONSOLE_FLUSHES: ") && kernel_number(flush_count) &&
        kernel_text("\nKERNEL_CONSOLE_FLUSH_TOTAL_PM: ") && kernel_number(flush_total) &&
        kernel_text("\nKERNEL_CONSOLE_FLUSH_MAX_PM: ") && kernel_number(flush_max) &&
        kernel_text("\nKERNEL_CONSOLE_IDLE_FLUSHES: ") && kernel_number(idle_count) &&
        kernel_text("\nKERNEL_CONSOLE_IDLE_TOTAL_PM: ") && kernel_number(idle_total) &&
        kernel_text("\nKERNEL_CONSOLE_SETTLE_TOTAL_PM: ") && kernel_number(settle_total) &&
        kernel_text("\nKERNEL_FB_VERIFY_FULL: ") && kernel_number(verify_full) &&
        kernel_text("\nKERNEL_FB_VERIFY_SKIPPED: ") && kernel_number(verify_skipped) &&
        kernel_text("\n") &&
#ifdef KERNEL_PROFILE_VGPU_FLUSH
        x86_profile_report() &&
#endif
        kernel_log(0, "KERNEL_CONSOLE_SHELL_CLEAN");
}
