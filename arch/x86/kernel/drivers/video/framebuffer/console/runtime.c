#include "runtime.h"
#include "../integration/kernel.h"
#include "kernel/console/video/service/console.h"
#include "kernel/console/video/measurement/budget.h"
#include "kernel/console/video/pointer/state.h"
#include "arch/x86/kernel/scheduler/integration/control.h"
#include "kernel/diagnostics/log.h"
#include "../drawing/draw.h"
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
#ifdef KERNEL_PS2_MOUSE
static KERNEL_CONSOLE_POINTER pointer;
static uint64_t pointer_frame = UINT64_MAX;
static uint64_t pointer_cells;
static uint32_t pointer_visibility = 2;
static uint32_t pointer_pending;
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
#ifdef KERNEL_PS2_MOUSE
    if (!kernel_pointer_valid(&pointer)) return 0;
    return x86_fb_kernel_group_pointer(column, row, scalars, count, 0xffffff, 0x081018, ticks, caret, &pointer.Image);
#else
    return x86_fb_kernel_group_caret(column, row, scalars, count, 0xffffff, 0x081018, ticks, caret);
#endif
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
    fb = &session->Env.Context->Info.Framebuffer;
    if (!fb->Base) { absent = 1; return kernel_log(1, "KERNEL_CONSOLE_SHELL_ABSENT"); }
    if (!kernel_console_service_init(&service, &view, fb->Width / KERNEL_CONSOLE_CELL_WIDTH, fb->Height / KERNEL_CONSOLE_CELL_HEIGHT)) return 0;
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
    if (absent) return 1;
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
    result = kernel_console_measure_batch_step_live(&measured, &batch);
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
        kernel_text("\n") && kernel_log(0, "KERNEL_CONSOLE_SHELL_CLEAN");
}
