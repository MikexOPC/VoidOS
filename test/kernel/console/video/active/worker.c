#include "worker.h"
#include "../../../../../kernel/console/video/measurement/budget.h"
#include "arch/x86/kernel/drivers/video/framebuffer/integration/kernel.h"
#include "arch/x86/kernel/scheduler/integration/control.h"
#include "kernel/diagnostics/log.h"
#include "arch/x86/kernel/exceptions/terminal.h"
#include "../profiling/profile.h"
static KERNEL_VIDEO_CONSOLE console;
static KERNEL_CONSOLE_RENDER render;
static KERNEL_CONSOLE_MEASUREMENT measured;
static X86_PS2_SESSION *source;
static uint32_t last_scalar, first_watchdog;
static KERNEL_CONSOLE_PROFILE profile;
#if defined(KERNEL_TEST_CONSOLE_BATCH)
static KERNEL_CONSOLE_BATCH batch;
#define DRAW_CELLS 8U
#else
#define DRAW_CELLS 1U
#endif

// ====== clock_read ======

static uint32_t clock_read(void *context)
{
    const X86_PM_CLOCK *clock = context;
    if (!source || clock != &source->Clock) kernel_scheduler_fail_stop();
    return x86_pm_read((unsigned short)clock->Port) & clock->Mask;
}

// ====== kernel_console_draw_mark ======

int kernel_console_draw_mark(uint32_t phase)
{
    if (!source || !measured.Busy || !render.Busy) return 0;
    return kernel_console_profile_mark(&profile, phase, clock_read(&source->Clock));
}

// ====== draw ======

static int draw(void *context, uint32_t column, uint32_t row, uint32_t scalar, uint64_t *ticks)
{
    int result;
    if (context != source || column || row) return 0;
    if (!kernel_console_draw_mark(0)) return 0;
    result = x86_fb_kernel_cell(2, 10, scalar, 0xffffff, 0x081018, ticks);
    if (result && !kernel_console_draw_mark(6)) result = 0;
    if (!result) {
        kernel_text("KERNEL_CONSOLE_DRAW_ENV: "); kernel_number(x86_exception_environment());
        kernel_text("\nKERNEL_CONSOLE_DRAW_WINDOW: "); kernel_number(x86_page_window_idle_valid(source->Env.Window));
        kernel_text("\nKERNEL_CONSOLE_DRAW_INVENTORY: ");
        kernel_number(x86_fb_kernel_inventory_verify(source->Env.Context, source->Env.Window->Pool,
            source->Env.Mapping, source->Env.Inventory));
        kernel_text("\n");
    }
    return result;
}

#if defined(KERNEL_TEST_CONSOLE_BATCH)
// ====== draw_group ======

static int draw_group(void *context, uint32_t column, uint32_t row,
    const uint32_t *scalars, uint32_t count, uint64_t *ticks)
{
    if (context != source || column || row || count != DRAW_CELLS || !kernel_console_draw_mark(0)) return 0;
    return x86_fb_kernel_group(2, 10, scalars, count, 0xffffff, 0x081018, ticks) && kernel_console_draw_mark(6);
}
#endif

// ====== kernel_console_active_worker ======

int kernel_console_active_worker(X86_PS2_SESSION *session)
{
    static const uint32_t glyphs[3] = {'A', 0xe8, 0x20ac};
    uint32_t index, result;
    if (!session || session != x86_ps2_session() || !session->Active || session->Quiet ||
        !x86_scheduler_task_valid() || x86_scheduler_index() != 1 || !x86_watchdog_valid() ||
        !x86_watchdog_budget_active() || session->Error || session->Losses ||
        !session->Clock.Ready || !session->Clock.Port || session->Clock.Port > 65535 ||
        (session->Clock.Mask != 0xffffff && session->Clock.Mask != UINT32_MAX)) return 0;
    if (!source) {
        source = session;
        first_watchdog = x86_watchdog_state()->Count;
        if (!kernel_console_profile_init(&profile, source->Clock.Mask) ||
            !kernel_console_init(&console, DRAW_CELLS, 1) ||
            !kernel_console_render_init(&render, &console, draw, source) ||
            !kernel_console_measure_init(&measured, &render, clock_read, &source->Clock,
                source->Clock.Mask)) return 0;
#if defined(KERNEL_TEST_CONSOLE_BATCH)
        if (!kernel_console_batch_init(&batch, &render, draw_group, source)) return 0;
#endif
    }
    if (source != session || measured.Cells >= 65536) return 0;
    for (index = 0; index < DRAW_CELLS; ++index) {
        last_scalar = glyphs[(measured.Cells + index) % 3];
        if (!kernel_console_step(&console, last_scalar)) return 0;
    }
#if defined(KERNEL_TEST_CONSOLE_BATCH)
    result = kernel_console_measure_batch_step(&measured, &batch);
#else
    result = kernel_console_measure_step(&measured);
#endif
    if (result != KERNEL_CONSOLE_RENDER_IDLE) {
        kernel_text("KERNEL_CONSOLE_DRAW_REJECTED_PM: "); kernel_number(measured.LastTicks);
        kernel_text("\n");
        return 0;
    }
    return !measured.Failed && !measured.Busy && render.CellsDone == measured.Cells;
}

// ====== profile_report ======

static int profile_report(void)
{
    static const char *names[6] = {"GATE", "PLAN", "INVENTORY", "PIXELS", "AUTHORITY", "RETURN"};
    uint32_t index;
    if (profile.Failed || profile.Next || profile.Cells != measured.Cells / DRAW_CELLS ||
        measured.Cells % DRAW_CELLS) return 0;
#if defined(KERNEL_TEST_CONSOLE_BATCH)
    if (batch.Failed || batch.Busy || batch.Groups != profile.Cells) return 0;
#endif
    if (!kernel_text("KERNEL_CONSOLE_PROFILE_TOTAL: ") || !kernel_number(profile.Total)) return 0;
    for (index = 0; index < 6; ++index) {
        if (!kernel_text("\nKERNEL_CONSOLE_PROFILE_") || !kernel_text(names[index]) ||
            !kernel_text("_SUM: ") || !kernel_number(profile.Sum[index]) ||
            !kernel_text("\nKERNEL_CONSOLE_PROFILE_") || !kernel_text(names[index]) ||
            !kernel_text("_MAX: ") || !kernel_number(profile.Max[index])) return 0;
    }
    return kernel_text("\n");
}

// ====== kernel_console_active_finish ======

int kernel_console_active_finish(const X86_PS2_SESSION *session)
{
    if (!source || session != source || session->Active || !session->Stop || !session->Quiet ||
        session->Error || session->Losses || !session->WorkerDone || !session->ScopeEnded ||
        measured.Failed || measured.Busy || measured.Cells < 32 ||
        render.CellsDone != measured.Cells || !measured.MaxTicks ||
        measured.MaxTicks > kernel_console_pm_limit() || !session->Irqs ||
        x86_watchdog_state()->Count <= first_watchdog) return 0;
    return profile_report() && kernel_text("KERNEL_CONSOLE_DRAW_BATCH_SIZE: ") && kernel_number(DRAW_CELLS) &&
        kernel_text("\nKERNEL_CONSOLE_DRAW_GROUPS: ") && kernel_number(profile.Cells) &&
        kernel_text("\nKERNEL_CONSOLE_DRAW_CELLS: ") && kernel_number(measured.Cells) &&
        kernel_text("\nKERNEL_CONSOLE_DRAW_MAX_PM: ") && kernel_number(measured.MaxTicks) &&
        kernel_text("\nKERNEL_CONSOLE_DRAW_PM_LIMIT: ") && kernel_number(kernel_console_pm_limit()) &&
        kernel_text("\nKERNEL_CONSOLE_DRAW_PM_GOAL: ") && kernel_number(kernel_console_pm_goal()) &&
        kernel_text("\nKERNEL_CONSOLE_DRAW_SLOW_CELLS: ") && kernel_number(measured.SlowCells) &&
        kernel_text("\nKERNEL_CONSOLE_DRAW_PM_MASK: ") && kernel_number(session->Clock.Mask) &&
        kernel_text("\nKERNEL_CONSOLE_DRAW_LAST_SCALAR: ") && kernel_number(last_scalar) &&
        kernel_text("\nKERNEL_CONSOLE_DRAW_LOSSES: ") && kernel_number(session->Losses) &&
        kernel_text("\nKERNEL_CONSOLE_DRAW_IRQS: ") && kernel_number(session->Irqs) &&
        kernel_text("\nKERNEL_CONSOLE_DRAW_HPET_START: ") && kernel_number(first_watchdog) &&
        kernel_text("\nKERNEL_CONSOLE_DRAW_HPET_END: ") && kernel_number(x86_watchdog_state()->Count) &&
        kernel_text("\n") && kernel_log(0, "KERNEL_CONSOLE_DRAW_ACTIVE_OK");
}
