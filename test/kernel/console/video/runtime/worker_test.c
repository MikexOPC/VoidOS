#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include "arch/x86/kernel/drivers/input/ps2/runtime/task.c"
static X86_PS2_SESSION session;
static uint32_t pending, flushes, observes, polls, yields, stop_at, checks;
static uint32_t injected, failure_stage, reporting;
static jmp_buf stopped;
#define CHECK(expression) do { ++checks; assert(expression); } while (0)

// ====== worker_boundary_mocks ======

X86_PS2_SESSION *x86_ps2_session(void) { return &session; }
uint64_t x86_scheduler_generation(void) { return 1; }
int x86_scheduler_task_valid(void) { return injected != 5; }
int kernel_log(uint32_t level, const char *message) { (void)level; return message != 0; }
__declspec(noreturn) void kernel_scheduler_fail_stop(void)
{
    CHECK(injected != 0);
    longjmp(stopped, 1);
}
void x86_scheduler_yield(void) { ++yields; CHECK(yields < 100); }
int x86_console_pending(X86_PS2_SESSION *owner)
{ CHECK(owner == &session); return injected == 1 ? -1 : pending != 0; }
// G5: the idle audit always succeeds here; the audit itself is covered by its own tests.
int x86_console_audit(X86_PS2_SESSION *owner) { CHECK(owner == &session); return 1; }
int x86_console_flush(X86_PS2_SESSION *owner)
{
    CHECK(owner == &session);
    if (injected == 2) return 0;
    if (pending) { --pending; ++flushes; }
    return 1;
}

// ====== unused_consumer_boundary ======

static void unexpected(void) { volatile int called = 1; CHECK(!called); }
void x86_ps2_consume(void) { unexpected(); }
void kernel_ps2_line_clear(KERNEL_PS2_LINE *line) { (void)line; unexpected(); }
uint32_t kernel_ps2_line_step(KERNEL_PS2_LINE *line, uint32_t input)
{ (void)line; (void)input; unexpected(); return 0; }
uint64_t kernel_ps2_decode(uint32_t state, uint32_t byte)
{ (void)state; (void)byte; unexpected(); return 0; }
uint64_t kernel_ps2_text(uint32_t state, uint32_t event)
{ (void)state; (void)event; unexpected(); return 0; }
uint64_t kernel_ps2_utf8(uint32_t scalar) { (void)scalar; unexpected(); return 0; }
uint64_t kernel_scheduler_payload_pop(uint32_t event, uint64_t source, uint64_t token,
    uint32_t task, uint64_t generation)
{ (void)event; (void)source; (void)token; (void)task; (void)generation; unexpected(); return 0; }
KERNEL_EVENT_REPLY kernel_scheduler_irq_drain(void) { KERNEL_EVENT_REPLY reply = {0}; unexpected(); return reply; }
uint32_t x86_watchdog_idle(const volatile uint32_t *ready) { (void)ready; unexpected(); return 0; }
int x86_scheduler_retained_valid(void) { unexpected(); return 0; }
const X86_DISPATCH_STATS *x86_scheduler_session_stats(void) { unexpected(); return 0; }
uint32_t x86_watchdog_failure(void) { unexpected(); return 0; }
uint32_t x86_watchdog_wake_detail(void) { unexpected(); return 0; }
uint32_t x86_watchdog_arm_detail(void) { unexpected(); return 0; }
uint64_t x86_watchdog_arm_registers(void) { unexpected(); return 0; }
X86_HPET_WATCHDOG *x86_watchdog_state(void) { unexpected(); return 0; }
uint32_t x86_watchdog_result(void) { unexpected(); return 0; }
int kernel_text(const char *text)
{
    CHECK(injected != 0 && text != NULL);
    reporting = strcmp(text, "PS2_WORKER_FAILURE_STAGE: ") == 0;
    return 1;
}
int kernel_number(BOOT_U64 number)
{
    CHECK(injected != 0);
    if (reporting) failure_stage = (uint32_t)number;
    return 1;
}
uint32_t x86_pm_read(unsigned short port) { CHECK(port == 0); return 0; }
int x86_ps2_shell_submit(void) { unexpected(); return 0; }
void x86_ps2_shell_close(void) { unexpected(); }
void x86_ps2_shell_task(void) { unexpected(); }
#ifdef KERNEL_PS2_MOUSE
void x86_ps2_mouse_task(void) { unexpected(); }
#endif
// Keyboard fault policy (B2) lives in ps2 interrupt.c; this fixture injects no faults.
int x86_ps2_fault_poll(X86_PS2_SESSION *owner) { (void)owner; return 0; }
int x86_ps2_fault_note(X86_PS2_SESSION *owner) { (void)owner; return 1; }
void x86_ps2_fault_valid(X86_PS2_SESSION *owner) { (void)owner; }
int x86_ps2_fault_resync(X86_PS2_SESSION *owner, int verdict) { (void)owner; (void)verdict; return 1; }
int x86_console_edit(X86_PS2_SESSION *owner) { (void)owner; unexpected(); return 0; }
int x86_console_submit(X86_PS2_SESSION *owner) { (void)owner; unexpected(); return 0; }

// ====== x86_scheduler_event_request ======

KERNEL_EVENT_REPLY x86_scheduler_event_request(uint32_t reason, uint32_t id, uint64_t token)
{
    KERNEL_EVENT_REPLY reply = {0};
#ifdef KERNEL_PS2_MOUSE
    CHECK(id == 6 && token == 0 && reason == X86_TASK_OBSERVE);
#else
    CHECK(id == 4 && token == 0 && reason == X86_TASK_OBSERVE);
#endif
    ++observes;
    if (observes == stop_at) session.Quiet = 1;
    reply.reply.status = KERNEL_SCHEDULER_OK;
    return reply;
}

// ====== x86_scheduler_irq_poll ======

int x86_scheduler_irq_poll(const volatile uint32_t *ready, uint32_t port, uint32_t mask, uint32_t ticks)
{
    CHECK(ready == &session.Ready && port == 0 && mask == 0);
#ifdef KERNEL_PS2_MOUSE
    CHECK(ticks == X86_PM_CLOCK_HZ / 1000);
#else
    CHECK(ticks == X86_PM_CLOCK_HZ / 1000 || ticks == X86_PM_CLOCK_HZ / 10);
#endif
    ++polls;
    if (injected == 4) session.Error = 1;
    return injected != 3;
}

// ====== main ======

int main(void)
{
    for (uint32_t groups = 2; groups <= 16; ++groups) {
        for (uint32_t point = 1; point < groups; ++point) {
            memset(&session, 0, sizeof(session));
            pending = groups; stop_at = point;
            flushes = observes = polls = yields = 0;
            worker_private();
            CHECK(!pending && flushes == groups && session.WorkerDone);
            CHECK(observes == point && polls == point - 1);
        }
    }
    memset(&session, 0, sizeof(session));
    session.Quiet = 1; pending = 8; flushes = observes = polls = yields = 0;
    worker_private();
    CHECK(!pending && flushes == 8 && !observes && !polls && session.WorkerDone);
    for (injected = 1; injected <= 5; ++injected) {
        memset(&session, 0, sizeof(session));
        pending = 0; stop_at = UINT32_MAX;
        flushes = observes = polls = yields = failure_stage = reporting = 0;
        if (!setjmp(stopped)) { worker_private(); CHECK(0); }
        CHECK(failure_stage == injected && !session.WorkerDone);
        CHECK(polls == (injected >= 3 ? 1U : 0U));
    }
    injected = 0;
    printf("PASS: worker stop/drain %u checks\n", checks);
    return 0;
}
