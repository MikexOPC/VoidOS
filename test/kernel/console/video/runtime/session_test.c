#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "arch/x86/kernel/drivers/video/framebuffer/console/runtime.c"
static X86_PS2_SESSION session;
static uint32_t checks, signals, reject;
#ifdef KERNEL_PS2_MOUSE
static uint32_t task_index, task_live, pointer_calls, pointer_reject, pointer_still;
#endif
#define CHECK(expression) do { ++checks; assert(expression); } while (0)

// ====== session_boundary_mocks ======

X86_PS2_SESSION *x86_ps2_session(void) { return &session; }
int kernel_log(uint32_t level, const char *message) { (void)level; return message != NULL; }
int kernel_text(const char *text) { return text != NULL; }
int kernel_number(BOOT_U64 number) { (void)number; return 1; }
int kernel_console_service_pending(const KERNEL_CONSOLE_SERVICE *owner) { (void)owner; return 0; }
int kernel_console_service_close(KERNEL_CONSOLE_SERVICE *owner) { (void)owner; return 1; }
int kernel_ps2_shell_output_unbind(KERNEL_SHELL_OUTPUT callback, void *context)
{ (void)callback; (void)context; return 1; }

// ====== unused_boundary_mocks ======

__declspec(noreturn) void kernel_scheduler_fail_stop(void) { CHECK(0); abort(); }
uint32_t x86_pm_read(unsigned short port) { (void)port; CHECK(0); return 0; }
#ifdef KERNEL_PS2_MOUSE
uint32_t x86_scheduler_index(void) { return task_index; }
int x86_scheduler_task_valid(void) { return task_live != 0; }
#else
uint32_t x86_scheduler_index(void) { CHECK(0); return 0; }
int x86_scheduler_task_valid(void) { CHECK(0); return 0; }
#endif
int kernel_ps2_shell_output_bind(KERNEL_SHELL_OUTPUT callback, void *context)
{ (void)callback; (void)context; CHECK(0); return 0; }
int kernel_console_service_init(KERNEL_CONSOLE_SERVICE *owner, KERNEL_VIDEO_CONSOLE *console,
    uint32_t columns, uint32_t rows)
{ (void)owner; (void)console; (void)columns; (void)rows; CHECK(0); return 0; }
int kernel_console_service_edit(KERNEL_CONSOLE_SERVICE *owner, const char *data, uint32_t length, uint32_t capacity)
{ (void)owner; (void)data; (void)length; (void)capacity; CHECK(0); return 0; }
int kernel_console_service_write(KERNEL_CONSOLE_SERVICE *owner, const char *data, uint32_t length, uint32_t capacity)
{ (void)owner; (void)data; (void)length; (void)capacity; CHECK(0); return 0; }
int kernel_console_service_submit(KERNEL_CONSOLE_SERVICE *owner) { (void)owner; CHECK(0); return 0; }
int kernel_console_service_render_begin(KERNEL_CONSOLE_SERVICE *owner) { (void)owner; CHECK(0); return 0; }
int kernel_console_service_render_end(KERNEL_CONSOLE_SERVICE *owner, int success)
{ (void)owner; (void)success; CHECK(0); return 0; }
int kernel_console_render_init(KERNEL_CONSOLE_RENDER *owner, KERNEL_VIDEO_CONSOLE *console,
    KERNEL_CONSOLE_DRAW draw, void *context)
{ (void)owner; (void)console; (void)draw; (void)context; CHECK(0); return 0; }
int kernel_console_batch_init(KERNEL_CONSOLE_BATCH *owner, KERNEL_CONSOLE_RENDER *renderer,
    KERNEL_CONSOLE_GROUP_DRAW draw, void *context)
{ (void)owner; (void)renderer; (void)draw; (void)context; CHECK(0); return 0; }
int kernel_console_measure_init(KERNEL_CONSOLE_MEASUREMENT *owner, KERNEL_CONSOLE_RENDER *renderer,
    KERNEL_CONSOLE_CLOCK read, void *context, uint32_t mask)
{ (void)owner; (void)renderer; (void)read; (void)context; (void)mask; CHECK(0); return 0; }
uint32_t kernel_console_measure_batch_step_live(KERNEL_CONSOLE_MEASUREMENT *owner, KERNEL_CONSOLE_BATCH *group)
{ (void)owner; (void)group; CHECK(0); return 0; }
int x86_fb_kernel_group_caret(BOOT_U32 column, BOOT_U32 row, const BOOT_U32 *scalars,
    BOOT_U32 count, BOOT_U32 foreground, BOOT_U32 background, BOOT_U64 *ticks, BOOT_U32 caret)
{ (void)column; (void)row; (void)scalars; (void)count; (void)foreground; (void)background;
    (void)ticks; (void)caret; CHECK(0); return 0; }

// ====== x86_scheduler_event_request ======

#ifdef KERNEL_PS2_MOUSE
int kernel_pointer_valid(const KERNEL_CONSOLE_POINTER *owner) { (void)owner; CHECK(0); return 0; }
int kernel_pointer_init(KERNEL_CONSOLE_POINTER *owner, KERNEL_VIDEO_CONSOLE *console)
{ (void)owner; (void)console; CHECK(0); return 0; }
int kernel_pointer_request(KERNEL_CONSOLE_POINTER *owner, int32_t dx, int32_t dy, uint32_t buttons)
{
    CHECK(owner == &pointer && dx == -7 && dy == 11 && buttons == 2);
    ++pointer_calls;
    if (!pointer_reject) { if (!pointer_still) ++owner->Moves; owner->Buttons = buttons; }
    return !pointer_reject;
}
int kernel_pointer_hide(KERNEL_CONSOLE_POINTER *owner) { (void)owner; CHECK(0); return 0; }
int kernel_pointer_close(KERNEL_CONSOLE_POINTER *owner) { (void)owner; CHECK(0); return 0; }
int x86_fb_kernel_group_pointer(BOOT_U32 column, BOOT_U32 row, const BOOT_U32 *scalars,
    BOOT_U32 count, BOOT_U32 foreground, BOOT_U32 background, BOOT_U64 *ticks,
    BOOT_U32 caret, const KERNEL_POINTER_IMAGE *image)
{ (void)image; return x86_fb_kernel_group_caret(column,row,scalars,count,foreground,background,ticks,caret); }
#endif

// ====== x86_scheduler_event_request ======

KERNEL_EVENT_REPLY x86_scheduler_event_request(uint32_t reason, uint32_t id, uint64_t token)
{
    KERNEL_EVENT_REPLY reply = {0};
#ifdef KERNEL_PS2_MOUSE
    CHECK(id == 6);
#else
    CHECK(id == 4);
#endif
    CHECK(reason == X86_TASK_SIGNAL && !token);
    ++signals;
    reply.reply.status = reject ? KERNEL_SCHEDULER_BAD_ARGUMENT : KERNEL_SCHEDULER_OK;
    return reply;
}

// ====== clean_session ======

static void clean_session(void)
{
    memset(&session, 0, sizeof(session));
    session.Quiet = session.WorkerDone = session.ConsumerDone = session.ShellDone = 1;
    session.Mouse.Done = 1;
    source = &session;
    absent = 1;
}

// ====== mouse_adapter_cases ======

#ifdef KERNEL_PS2_MOUSE
static void mouse_adapter_cases(void)
{
    uint32_t saved_signals = signals;
    clean_session(); session.Active = 1; absent = 0;
    task_index = 3; task_live = 1; reject = 0;
    CHECK(x86_console_mouse(&session, -7, 11, 2) && pointer_calls == 1 && signals == saved_signals + 1);
    for (task_index = 0; task_index < 5; ++task_index)
        if (task_index != 3) CHECK(!x86_console_mouse(&session, -7, 11, 2) && pointer_calls == 1);
    task_index = 3; task_live = 0; CHECK(!x86_console_mouse(&session,-7,11,2)); task_live = 1;
    session.Active = 0; CHECK(!x86_console_mouse(&session,-7,11,2)); session.Active = 1;
    session.Error = 1; CHECK(!x86_console_mouse(&session,-7,11,2)); session.Error = 0;
    session.Losses = 1; CHECK(!x86_console_mouse(&session,-7,11,2)); session.Losses = 0;
    service.Busy = 2; CHECK(!x86_console_mouse(&session,-7,11,2)); service.Busy = 0;
    service.Closed = 1; CHECK(!x86_console_mouse(&session,-7,11,2)); service.Closed = 0;
    service.Failed = 1; CHECK(!x86_console_mouse(&session,-7,11,2)); service.Failed = 0;
    CHECK(!x86_console_mouse(NULL,-7,11,2));
    {
        X86_PS2_SESSION copy = session;
        CHECK(!x86_console_mouse(&copy,-7,11,2));
    }
    CHECK(pointer_calls == 1 && signals == saved_signals + 1);
    pointer_reject = 1; CHECK(!x86_console_mouse(&session,-7,11,2) && pointer_calls == 2);
    CHECK(signals == saved_signals + 1);
    pointer_reject = 0; session.Stop = 1; CHECK(x86_console_mouse(&session,-7,11,2));
    session.Stop = 0; absent = 1; CHECK(x86_console_mouse(&session,-7,11,2));
    CHECK(pointer_calls == 2 && signals == saved_signals + 1);
    absent = 0; pointer_still = 1; pointer_pending = 0;
    CHECK(x86_console_mouse(&session,-7,11,2) && pointer_calls == 3);
    CHECK(!pointer_pending && signals == saved_signals + 1);
    pointer_still = 0;
}
#endif

// ====== main ======

int main(void)
{
    for (uint32_t index = 0; index < 1000; ++index) {
        reject = 0; CHECK(wake());
        reject = 1; CHECK(!wake());
    }
    CHECK(signals == 2000);
    clean_session(); CHECK(x86_console_finish(&session));
    CHECK(!x86_console_finish(NULL));
#ifdef KERNEL_PS2_MOUSE
    session.Mouse.Done = 0; CHECK(!x86_console_finish(&session));
    clean_session(); session.Mouse.Token.Id = 1; CHECK(!x86_console_finish(&session));
    clean_session(); session.Mouse.RouteOwned = 1; CHECK(!x86_console_finish(&session));
    clean_session(); session.Mouse.Decoder.State = 1; CHECK(!x86_console_finish(&session));
    clean_session(); session.Mouse.Buttons = 1; CHECK(!x86_console_finish(&session));
    clean_session(); session.Mouse.Losses = 1; CHECK(!x86_console_finish(&session));
#endif
    clean_session(); session.Active = 1; CHECK(!x86_console_finish(&session));
    clean_session(); session.WorkerDone = 0; CHECK(!x86_console_finish(&session));
    clean_session(); session.Quiet = 0; CHECK(!x86_console_finish(&session));
    clean_session(); session.ConsumerDone = 0; CHECK(!x86_console_finish(&session));
    clean_session(); session.ShellDone = 0; CHECK(!x86_console_finish(&session));
    clean_session(); session.Error = 1; CHECK(!x86_console_finish(&session));
    clean_session(); session.Losses = 1; CHECK(!x86_console_finish(&session));
#ifdef KERNEL_PS2_MOUSE
    mouse_adapter_cases();
#endif
    printf("PASS: console wake/close %u checks; scheduler and video MOCK\n", checks);
    return 0;
}
