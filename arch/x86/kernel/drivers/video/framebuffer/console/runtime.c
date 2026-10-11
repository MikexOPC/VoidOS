#include "runtime.h"
#include "../integration/kernel.h"
#include "kernel/console/video/service/console.h"
#include "kernel/console/video/measurement/budget.h"
#include "arch/x86/kernel/scheduler/integration/control.h"
#include "kernel/diagnostics/log.h"
static KERNEL_CONSOLE_SERVICE service;
static KERNEL_VIDEO_CONSOLE view;
static KERNEL_CONSOLE_RENDER render;
static KERNEL_CONSOLE_BATCH batch;
static KERNEL_CONSOLE_MEASUREMENT measured;
static X86_PS2_SESSION *source;
static uint32_t absent;

// ====== task_valid ======

static int task_valid(X86_PS2_SESSION *session, uint32_t index)
{
    return source && session == source && session == x86_ps2_session() && session->Active &&
        !session->Error && !session->Losses && x86_scheduler_task_valid() && x86_scheduler_index() == index;
}

// ====== wake ======

static int wake(void)
{
    return x86_scheduler_event_request(X86_TASK_SIGNAL, 4, 0).reply.status == KERNEL_SCHEDULER_OK;
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

// ====== draw_group ======

static int draw_group(void *context, uint32_t column, uint32_t row,
    const uint32_t *scalars, uint32_t count, uint64_t *ticks)
{
    return context == source && task_valid(source, 1) && service.Busy == 2 &&
        x86_fb_kernel_group(column, row, scalars, count, 0xffffff, 0x081018, ticks);
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
    return kernel_console_service_init(&service, &view, fb->Width / 8, fb->Height / 16) &&
        kernel_console_render_init(&render, &view, draw_cell, source) &&
        kernel_console_batch_init(&batch, &render, draw_group, source) &&
        kernel_console_measure_init(&measured, &render, clock_read, source, source->Clock.Mask) &&
        kernel_ps2_shell_output_bind(output, source) && kernel_log(0, "KERNEL_CONSOLE_SHELL_OPEN");
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
    return absent ? 0 : kernel_console_service_pending(&service);
}

// ====== x86_console_flush ======

int x86_console_flush(X86_PS2_SESSION *session)
{
    uint32_t result;
    if (!task_valid(session, 1)) return 0;
    if (absent) return 1;
    if (!kernel_console_service_render_begin(&service)) return 0;
    result = kernel_console_measure_batch_step(&measured, &batch);
    if (!kernel_console_service_render_end(&service, result != 0)) return 0;
    return result != 0 && !measured.Failed && !batch.Failed && !render.Busy;
}

// ====== x86_console_finish ======

int x86_console_finish(X86_PS2_SESSION *session)
{
    if (!source || session != source || session->Active || !session->Quiet ||
        !session->WorkerDone || !session->ConsumerDone || !session->ShellDone || session->Error || session->Losses)
        return 0;
    if (absent) return kernel_log(0, "KERNEL_CONSOLE_SHELL_SERIAL_CLEAN");
    if (kernel_console_service_pending(&service) != 0 || measured.Failed || batch.Failed ||
        !measured.Cells || render.CellsDone != measured.Cells || !batch.Groups ||
        !kernel_ps2_shell_output_unbind(output, source) || !kernel_console_service_close(&service)) return 0;
    return kernel_text("KERNEL_CONSOLE_SHELL_CELLS: ") && kernel_number(measured.Cells) &&
        kernel_text("\nKERNEL_CONSOLE_SHELL_GROUPS: ") && kernel_number(batch.Groups) &&
        kernel_text("\nKERNEL_CONSOLE_SHELL_MAX_PM: ") && kernel_number(measured.MaxTicks) &&
        kernel_text("\n") && kernel_log(0, "KERNEL_CONSOLE_SHELL_CLEAN");
}
