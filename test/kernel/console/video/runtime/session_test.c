#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "arch/x86/kernel/drivers/video/framebuffer/console/runtime.h"
#include "arch/x86/kernel/scheduler/integration/control.h"
#include "arch/x86/kernel/drivers/video/framebuffer/integration/kernel.h"
#include "arch/x86/kernel/drivers/video/framebuffer/pixels/session.h"
#include "kernel/console/video/render.h"
#include "kernel/diagnostics/log.h"
// G6: runtime.c is linked, not included; the test drives the console object through its owner.
static X86_PS2_SESSION session;
static X86_CONSOLE console;
static uint32_t checks, signals, reject, task_index, task_live, submits;
#ifdef KERNEL_PS2_MOUSE
static uint32_t pointer_calls, pointer_reject, pointer_still, lock_held, lock_pairs, irq_saved;
static int32_t expect_dx = -7, expect_dy = 11;
static uint32_t expect_buttons = 2;
static KERNEL_LOCK_CONTEXT lock_context;
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
uint32_t x86_scheduler_index(void) { return task_index; }
int x86_scheduler_task_valid(void) { return task_live != 0; }
int x86_scheduler_driver_task_valid(void) { return x86_scheduler_task_valid(); }
int kernel_console_service_clip(KERNEL_CONSOLE_SERVICE *owner, uint32_t columns, uint32_t rows)
{ (void)owner; (void)columns; (void)rows; CHECK(0); return 0; }
int kernel_ps2_shell_output_bind(KERNEL_SHELL_OUTPUT callback, void *context)
{ (void)callback; (void)context; CHECK(0); return 0; }
int kernel_console_service_init(KERNEL_CONSOLE_SERVICE *owner, KERNEL_VIDEO_CONSOLE *view,
    uint32_t columns, uint32_t rows)
{ (void)owner; (void)view; (void)columns; (void)rows; CHECK(0); return 0; }
int kernel_console_service_edit(KERNEL_CONSOLE_SERVICE *owner, const char *data, uint32_t length, uint32_t capacity)
{ (void)owner; (void)data; (void)length; (void)capacity; CHECK(0); return 0; }
int kernel_console_service_write(KERNEL_CONSOLE_SERVICE *owner, const char *data, uint32_t length, uint32_t capacity)
{ (void)owner; (void)data; (void)length; (void)capacity; CHECK(0); return 0; }
int kernel_console_service_submit(KERNEL_CONSOLE_SERVICE *owner) { CHECK(owner == &console.Service); ++submits; return 1; }
int kernel_console_service_render_begin(KERNEL_CONSOLE_SERVICE *owner) { (void)owner; CHECK(0); return 0; }
int kernel_console_service_render_end(KERNEL_CONSOLE_SERVICE *owner, int success)
{ (void)owner; (void)success; CHECK(0); return 0; }
int kernel_console_render_init(KERNEL_CONSOLE_RENDER *owner, KERNEL_VIDEO_CONSOLE *view,
    KERNEL_CONSOLE_DRAW draw, void *context)
{ (void)owner; (void)view; (void)draw; (void)context; CHECK(0); return 0; }
int kernel_console_batch_init(KERNEL_CONSOLE_BATCH *owner, KERNEL_CONSOLE_RENDER *renderer,
    KERNEL_CONSOLE_GROUP_DRAW draw, void *context)
{ (void)owner; (void)renderer; (void)draw; (void)context; CHECK(0); return 0; }
int kernel_console_measure_init(KERNEL_CONSOLE_MEASUREMENT *owner, KERNEL_CONSOLE_RENDER *renderer,
    KERNEL_CONSOLE_CLOCK read, void *context, uint32_t mask)
{ (void)owner; (void)renderer; (void)read; (void)context; (void)mask; CHECK(0); return 0; }
uint32_t kernel_console_measure_batch_step_live(KERNEL_CONSOLE_MEASUREMENT *owner, KERNEL_CONSOLE_BATCH *group)
{ (void)owner; (void)group; CHECK(0); return 0; }
// H1-A verification cache of the pixel layer: not part of the session under test.
void x86_fb_verify_reset(void) {}
void x86_fb_verify_counts(BOOT_U64 *full, BOOT_U64 *skipped, BOOT_U64 *accepted)
{ if (full) *full = 0; if (skipped) *skipped = 0; if (accepted) *accepted = 0; }
int x86_fb_kernel_audit(void) { return 1; }
// Geometry A: no redirected target in the keyboard session; the grid follows the GOP.
const X86_FB_TARGET *x86_fb_target(void) { return 0; }
int x86_fb_kernel_group_caret(BOOT_U32 column, BOOT_U32 row, const BOOT_U32 *scalars,
    BOOT_U32 count, BOOT_U32 foreground, BOOT_U32 background, BOOT_U64 *ticks, BOOT_U32 caret)
{ (void)column; (void)row; (void)scalars; (void)count; (void)foreground; (void)background;
    (void)ticks; (void)caret; CHECK(0); return 0; }

// ====== x86_scheduler_event_request ======

#ifdef KERNEL_PS2_MOUSE
int kernel_pointer_valid(const KERNEL_CONSOLE_POINTER *owner) { (void)owner; CHECK(0); return 0; }
int kernel_pointer_init(KERNEL_CONSOLE_POINTER *owner, KERNEL_VIDEO_CONSOLE *view)
{ (void)owner; (void)view; CHECK(0); return 0; }
int kernel_pointer_request(KERNEL_CONSOLE_POINTER *owner, int32_t dx, int32_t dy, uint32_t buttons)
{
    CHECK(owner == &console.Pointer && dx == expect_dx && dy == expect_dy && buttons == expect_buttons && lock_held);
    ++pointer_calls;
    if (!pointer_reject) { if (!pointer_still) ++owner->Moves; owner->Buttons = buttons; }
    return !pointer_reject;
}
int kernel_pointer_hide(KERNEL_CONSOLE_POINTER *owner) { (void)owner; CHECK(0); return 0; }

// ====== pointer_lock_mocks ======

uint64_t ks_irq_save(void) { CHECK(!irq_saved); irq_saved = 1; return 512; }
void ks_irq_restore(uint64_t saved_if) { CHECK(irq_saved && saved_if == 512 && !lock_held); irq_saved = 0; }
KERNEL_LOCK_CONTEXT *kernel_lock_current_context(void) { CHECK(irq_saved); return &lock_context; }
int kernel_lock_initialize(KERNEL_LOCK *lock, uint32_t rank) { (void)lock; (void)rank; CHECK(0); return 0; }
int kernel_lock_acquire(KERNEL_LOCK *lock, KERNEL_LOCK_CONTEXT *context, KERNEL_LOCK_GUARD *guard)
{
    CHECK(lock == &console.PointerLock && context == &lock_context && guard && irq_saved && !lock_held);
    lock_held = 1;
    return 1;
}
int kernel_lock_release(KERNEL_LOCK_CONTEXT *context, KERNEL_LOCK_GUARD *guard)
{
    CHECK(context == &lock_context && guard && irq_saved && lock_held);
    lock_held = 0; ++lock_pairs;
    return 1;
}
int kernel_pointer_clamp(KERNEL_CONSOLE_POINTER *owner) { (void)owner; CHECK(0); return 0; }
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

// ====== request_boundary_mocks ======

int kernel_console_requests_initialize(KERNEL_CONSOLE_REQUEST_QUEUE *owner)
{ (void)owner; CHECK(0); return 0; }
int x86_console_requests_step(X86_CONSOLE *owner)
{ (void)owner; CHECK(0); return 0; }
int x86_console_requests_pending(X86_CONSOLE *owner)
{ (void)owner; CHECK(0); return -1; }

// ====== request_boundary_mock ======

int x86_console_requests_send(X86_CONSOLE *owner, uint32_t operation,
    const void *text, uint32_t length, uint32_t capacity)
{
    CHECK(owner == &console && operation == KCR_SUBMIT && !text && !length && !capacity);
    ++submits;
#ifdef KERNEL_PS2_MOUSE
    return x86_scheduler_event_request(X86_TASK_SIGNAL,6,0).reply.status == KERNEL_SCHEDULER_OK;
#else
    return x86_scheduler_event_request(X86_TASK_SIGNAL,4,0).reply.status == KERNEL_SCHEDULER_OK;
#endif
}

// ====== clean_session ======

static void clean_session(void)
{
    memset(&session, 0, sizeof(session));
    memset(&console, 0, sizeof(console));
    session.Quiet = session.WorkerDone = session.ConsumerDone = session.ShellDone = 1;
    session.Mouse.Done = 1;
    session.Console = &console;
    console.Source = &session;
    console.Absent = 1;
}

// ====== mouse_adapter_cases ======

#ifdef KERNEL_PS2_MOUSE
static void mouse_adapter_cases(void)
{
    uint32_t saved_signals = signals;
    clean_session(); session.Active = 1; console.Absent = 0;
    task_index = 3; task_live = 1; reject = 0;
    CHECK(x86_console_mouse(&session, -7, 11, 2) && pointer_calls == 1 && signals == saved_signals + 1);
    for (task_index = 0; task_index < 5; ++task_index)
        if (task_index != 3) CHECK(!x86_console_mouse(&session, -7, 11, 2) && pointer_calls == 1);
    task_index = 3; task_live = 0; CHECK(!x86_console_mouse(&session,-7,11,2)); task_live = 1;
    session.Active = 0; CHECK(!x86_console_mouse(&session,-7,11,2)); session.Active = 1;
    session.Error = 1; CHECK(!x86_console_mouse(&session,-7,11,2)); session.Error = 0;
    session.Losses = 1; CHECK(!x86_console_mouse(&session,-7,11,2)); session.Losses = 0;
    {
        uint32_t calls = pointer_calls, saved = signals, pairs = lock_pairs;
        console.Service.Busy = 2;
        CHECK(x86_console_mouse(&session,-7,11,2) && x86_console_mouse(&session,-7,11,1));
        CHECK(pointer_calls == calls && signals == saved && console.Deferred && lock_pairs == pairs + 2);
        CHECK(console.DeferredX == -14 && console.DeferredY == 22 && console.DeferredButtons == 1);
        CHECK(x86_console_pointer_deferred(&console) && pointer_calls == calls && console.Deferred);
        console.Service.Busy = 0;
        expect_dx = -14; expect_dy = 22; expect_buttons = 1; console.PointerPending = 0;
        CHECK(x86_console_pointer_deferred(&console) && pointer_calls == calls + 1);
        CHECK(!console.Deferred && !console.DeferredX && !console.DeferredY && console.PointerPending);
        CHECK(x86_console_pointer_deferred(&console) && pointer_calls == calls + 1);
        console.DeferredX = INT32_MAX - 1; console.Service.Busy = 2;
        CHECK(x86_console_mouse(&session,5,0,0) && console.DeferredX == INT32_MAX);
        console.Service.Busy = 0; console.Deferred = 0; console.DeferredX = console.DeferredY = 0;
        console.PointerPending = 0;
        expect_dx = -7; expect_dy = 11; expect_buttons = 2;
    }
    console.Service.Closed = 1; CHECK(!x86_console_mouse(&session,-7,11,2)); console.Service.Closed = 0;
    console.Service.Failed = 1; CHECK(!x86_console_mouse(&session,-7,11,2)); console.Service.Failed = 0;
    CHECK(!x86_console_mouse(NULL,-7,11,2));
    {
        X86_PS2_SESSION copy = session;
        CHECK(!x86_console_mouse(&copy,-7,11,2));
    }
    CHECK(pointer_calls == 2 && signals == saved_signals + 1);
    pointer_reject = 1; CHECK(!x86_console_mouse(&session,-7,11,2) && pointer_calls == 3);
    CHECK(signals == saved_signals + 1);
    pointer_reject = 0; session.Stop = 1; CHECK(x86_console_mouse(&session,-7,11,2));
    session.Stop = 0; console.Absent = 1; CHECK(x86_console_mouse(&session,-7,11,2));
    CHECK(pointer_calls == 3 && signals == saved_signals + 1);
    console.Absent = 0; pointer_still = 1; console.PointerPending = 0;
    CHECK(x86_console_mouse(&session,-7,11,2) && pointer_calls == 4);
    CHECK(!console.PointerPending && signals == saved_signals + 1);
    pointer_still = 0;
}
#endif

// ====== main ======

int main(void)
{
    // The wake signal through the public submit (task 0): accepted, then refused by the scheduler.
    clean_session(); session.Active = 1; console.Absent = 0; task_live = 1; task_index = 0;
    for (uint32_t index = 0; index < 1000; ++index) {
        reject = 0; CHECK(x86_console_submit(&session));
        reject = 1; CHECK(!x86_console_submit(&session));
    }
    CHECK(signals == 2000 && submits == 2000);
    task_index = 1; CHECK(!x86_console_submit(&session) && submits == 2000);
    session.Console = 0; task_index = 0; CHECK(!x86_console_submit(&session) && submits == 2000);
    clean_session(); session.Active = 1; console.Source = 0; CHECK(!x86_console_submit(&session));
    reject = 0;
    clean_session(); CHECK(x86_console_finish(&session));
    CHECK(!x86_console_finish(NULL));
#ifdef KERNEL_PS2_MOUSE
    session.Mouse.Done = 0; CHECK(!x86_console_finish(&session));
    clean_session(); session.Mouse.Token.Id = 1; CHECK(!x86_console_finish(&session));
    clean_session(); session.Mouse.RouteOwned = 1; CHECK(!x86_console_finish(&session));
    clean_session(); session.Mouse.Decoder.State = 1; CHECK(!x86_console_finish(&session));
    clean_session(); session.Mouse.Buttons = 1; CHECK(!x86_console_finish(&session));
    clean_session(); session.Mouse.Losses = 1; session.Mouse.Rejected = 2; session.Mouse.Saturated = 3;
    CHECK(x86_console_finish(&session)); // Counted, recovered framing errors are not a cleanup failure.
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
