#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include "arch/x86/kernel/drivers/video/framebuffer/console/runtime.h"
#include "arch/x86/kernel/scheduler/integration/control.h"

static X86_CONSOLE console;
static X86_PS2_SESSION session;
static uint32_t index, live = 1, pending, state, closed, pushes, waits, signals, mode;
static uint32_t applied, fail_consume;
static uint64_t epoch;
static const char payload[] = "test";

// ====== scheduler_boundary ======

uint32_t x86_scheduler_index(void) { return index; }
int x86_scheduler_task_valid(void) { return live != 0; }
__declspec(noreturn) void kernel_scheduler_fail_stop(void) { abort(); }

// ====== x86_scheduler_event_request ======

KERNEL_EVENT_REPLY x86_scheduler_event_request(uint32_t reason, uint32_t id, uint64_t token) {
    KERNEL_EVENT_REPLY reply = {0};
#ifdef KERNEL_PS2_MOUSE
    assert(id == 6);
#else
    assert(id == 4);
#endif
    reply.reply.status = KERNEL_SCHEDULER_OK;
    if (reason == X86_TASK_OBSERVE) {
        assert(token == 0);
        reply.epoch = epoch;
        if (mode == 4) reply.reply.status = KERNEL_SCHEDULER_BAD_ARGUMENT;
    } else if (reason == X86_TASK_WAIT) {
        ++waits;
        if (mode == 1) assert(token + 1 == epoch);
        else assert(token == epoch);
        if (mode != 3) pending = 0;
        if (mode != 1) ++epoch;
    } else {
        assert(reason == X86_TASK_SIGNAL && token == 0);
        ++signals;
        ++epoch;
    }
    return reply;
}

// ====== queue_boundary ======

uint32_t kernel_console_requests_push(KERNEL_CONSOLE_REQUEST_QUEUE *queue, uint32_t operation,
    const void *text, uint32_t length, uint32_t capacity) {
    assert(queue == &console.Requests && operation == KCR_WRITE && text == payload);
    assert(length == 4 && capacity == sizeof(payload));
    ++pushes;
    if (pending == 8) {
        if (mode == 1) { ++epoch; pending = 0; }
        return KCR_FULL;
    }
    ++pending;
    return KCR_OK;
}

uint32_t kernel_console_requests_consume(KERNEL_CONSOLE_REQUEST_QUEUE *queue,
    KERNEL_CONSOLE_SERVICE *service) {
    assert(queue == &console.Requests && service == &console.Service);
    if (fail_consume) return KCR_STATE;
    if (!pending) return KCR_EMPTY;
    --pending;
    ++applied;
    return KCR_OK;
}

uint32_t kernel_console_requests_query(KERNEL_CONSOLE_REQUEST_QUEUE *queue,
    KERNEL_CONSOLE_REQUEST_INFO *output) {
    assert(queue == &console.Requests);
    output->State = state;
    output->Pending = pending;
    return KCR_OK;
}

uint32_t kernel_console_requests_drain(KERNEL_CONSOLE_REQUEST_QUEUE *queue) {
    assert(queue == &console.Requests && !pending);
    state = KCR_DRAINING;
    return KCR_OK;
}

uint32_t kernel_console_requests_close(KERNEL_CONSOLE_REQUEST_QUEUE *queue) {
    assert(queue == &console.Requests && state == KCR_DRAINING);
    ++closed;
    state = KCR_CLOSED;
    return KCR_OK;
}

// ====== send_cases ======

static void send_cases(void) {
    index = 0;
    for (mode = 1; mode <= 2; ++mode) {
        pending = 8; pushes = waits = signals = 0; epoch = 10;
        assert(x86_console_requests_send(&console, KCR_WRITE, payload, 4, sizeof(payload)));
        assert(pushes == 2 && waits == 1 && signals == 1 && pending == 1);
    }
    mode = 3; pending = 8; pushes = waits = signals = 0;
    assert(!x86_console_requests_send(&console, KCR_WRITE, payload, 4, sizeof(payload)));
    assert(pushes == 1024 && waits == 1024 && signals == 0 && pending == 8);
    mode = 4; pushes = 0;
    assert(!x86_console_requests_send(&console, KCR_WRITE, payload, 4, sizeof(payload)) && !pushes);
    mode = 0; index = 1;
    assert(!x86_console_requests_send(&console, KCR_WRITE, payload, 4, sizeof(payload)));
    index = 2; live = 0;
    assert(!x86_console_requests_send(&console, KCR_WRITE, payload, 4, sizeof(payload)));
    live = 1;
}

// ====== worker_cases ======

static void worker_cases(void) {
    index = 1; state = KCR_OPEN; pending = 8; signals = applied = 0;
    assert(x86_console_requests_step(&console));
    assert(applied == 8 && signals == 8 && pending == 0);
    assert(x86_console_requests_step(&console) && signals == 8);
    pending = 1; fail_consume = 1;
    assert(!x86_console_requests_step(&console) && pending == 1 && signals == 8);
    fail_consume = 0;
    assert(x86_console_requests_pending(&console) == 1);
    pending = 0;
    assert(x86_console_requests_pending(&console) == 0 && !closed);
    session.Quiet = 1;
    assert(x86_console_requests_pending(&console) == 1 && !closed);
    session.ConsumerDone = 1;
    assert(x86_console_requests_pending(&console) == 1 && !closed);
    session.ShellDone = 1;
    assert(x86_console_requests_pending(&console) == 0 && closed == 1 && console.RequestsClosed);
    assert(x86_console_requests_pending(&console) == 0 && closed == 1);
    assert(x86_console_requests_step(&console) && applied == 8);
}

// ====== main ======

int main(void) {
    console.Source = &session;
    send_cases();
    worker_cases();
    puts("PASS: native console adapter, full queue, signal-before-wait, retry bound, worker budget and producer quiescence; queue/scheduler MOCK");
    return 0;
}
