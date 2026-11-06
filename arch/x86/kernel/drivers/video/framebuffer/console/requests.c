#include "runtime.h"
#include "arch/x86/kernel/scheduler/integration/control.h"

// ====== event_id ======

static uint32_t event_id(void) {
#ifdef KERNEL_PS2_MOUSE
    return 6;
#else
    return 4;
#endif
}

// ====== signal_space ======

static int signal_space(void) {
    return x86_scheduler_event_request(X86_TASK_SIGNAL, event_id(), 0).reply.status == KERNEL_SCHEDULER_OK;
}

// ====== kernel_console_requests_fail_stop ======

__declspec(noreturn) void kernel_console_requests_fail_stop(void) { kernel_scheduler_fail_stop(); }

// ====== x86_console_requests_send ======

int x86_console_requests_send(X86_CONSOLE *self, uint32_t operation,
    const void *text, uint32_t length, uint32_t capacity) {
    if (!self || !x86_scheduler_task_valid() ||
        (x86_scheduler_index() != 0 && x86_scheduler_index() != 2)) return 0;
    for (uint32_t retries = 0; retries < 1024; ++retries) {
        KERNEL_EVENT_REPLY observed = x86_scheduler_event_request(X86_TASK_OBSERVE, event_id(), 0);
        uint32_t result;
        if (observed.reply.status != KERNEL_SCHEDULER_OK) return 0;
        result = kernel_console_requests_push(&self->Requests, operation, text, length, capacity);
        if (result == KCR_OK) return signal_space();
        if (result != KCR_FULL ||
            x86_scheduler_event_request(X86_TASK_WAIT, event_id(), observed.epoch).reply.status != KERNEL_SCHEDULER_OK)
            return 0;
    }
    return 0;
}

// ====== x86_console_requests_step ======

int x86_console_requests_step(X86_CONSOLE *self) {
    if (!self || !x86_scheduler_task_valid() || x86_scheduler_index() != 1) return 0;
    if (self->RequestsClosed) return 1;
    for (uint32_t index = 0; index < 8; ++index) {
        uint32_t result = kernel_console_requests_consume(&self->Requests, &self->Service);
        if (result == KCR_EMPTY) return 1;
        if (result != KCR_OK || !signal_space()) return 0;
    }
    return 1;
}

// ====== x86_console_requests_pending ======

int x86_console_requests_pending(X86_CONSOLE *self) {
    KERNEL_CONSOLE_REQUEST_INFO info;
    if (!self || !x86_scheduler_task_valid() || x86_scheduler_index() != 1) return -1;
    if (self->RequestsClosed) return 0;
    if (kernel_console_requests_query(&self->Requests, &info) != KCR_OK || info.State != KCR_OPEN) return -1;
    if (info.Pending) return 1;
    if (!self->Source->Quiet) return 0;
    if (!self->Source->ConsumerDone || !self->Source->ShellDone) return 1;
    if (kernel_console_requests_drain(&self->Requests) != KCR_OK ||
        kernel_console_requests_close(&self->Requests) != KCR_OK) return -1;
    self->RequestsClosed = 1;
    return 0;
}
