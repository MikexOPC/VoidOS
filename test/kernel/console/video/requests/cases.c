#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "test.h"

static __declspec(thread) KERNEL_LOCK_CONTEXT context;
static __declspec(thread) uint32_t prepared;
static KERNEL_LOCK_WAIT wait;
uint64_t kernel_console_ap_channel(uint32_t operation, uint32_t cpu, uint64_t token, uint32_t producer);

// ====== kernel_lock_current_context ======

KERNEL_LOCK_CONTEXT *kernel_lock_current_context(void) {
    assert(!ks_irq_state());
    return prepared ? &context : NULL;
}

// ====== kernel_console_requests_fail_stop ======

__declspec(noreturn) void kernel_console_requests_fail_stop(void) {
    fputs("UNEXPECTED_CONSOLE_REQUEST_FAULT\n", stderr);
    ExitProcess(87);
}

// ====== requests_context_prepare ======

void requests_context_prepare(void) {
    assert(!prepared);
    assert(kernel_lock_context_initialize(&context, &wait));
    prepared = 1;
}

// ====== requests_context_disable ======

void requests_context_disable(void) { prepared = 0; }

// ====== empty_info ======

static void empty_info(KERNEL_CONSOLE_REQUEST_QUEUE *queue, uint32_t state) {
    KERNEL_CONSOLE_REQUEST_INFO info = {0};
    assert(kernel_console_requests_query(queue, &info) == KCR_OK);
    assert(info.State == state && !info.Pending);
    assert(!context.Depth && ks_irq_state() == 0x200);
}

// ====== boundaries ======

static void boundaries(void) {
    KERNEL_CONSOLE_REQUEST_QUEUE queue = {0}, copy;
    KERNEL_CONSOLE_REQUEST output, before;
    KERNEL_CONSOLE_REQUEST_INFO info;
    uint8_t bytes[257];
    _Alignas(8) uint8_t unaligned[sizeof(queue) + 8] = {0};
    memset(bytes, 'a', sizeof(bytes));
    memset(&output, 0xa5, sizeof(output));
    before = output;
    assert(!kernel_console_requests_initialize(NULL));
    assert(!kernel_console_requests_initialize((KERNEL_CONSOLE_REQUEST_QUEUE *)(unaligned + 1)));
    assert(kernel_console_requests_take(&queue, &output) == KCR_INVALID);
    assert(kernel_console_requests_initialize(&queue));
    assert(!kernel_console_requests_initialize(&queue));
    copy = queue;
    assert(kernel_console_requests_take(&copy, &output) == KCR_INVALID);
    assert(kernel_console_requests_take(&queue, &output) == KCR_EMPTY);
    assert(!memcmp(&output, &before, sizeof(output)));
    assert(kernel_console_requests_query(&queue, (KERNEL_CONSOLE_REQUEST_INFO *)&queue) == KCR_INVALID);
    assert(kernel_console_requests_take(&queue, (KERNEL_CONSOLE_REQUEST *)&context) == KCR_INVALID);
    assert(kernel_console_requests_take(&queue, NULL) == KCR_INVALID);
    assert(kernel_console_requests_take(&queue, (KERNEL_CONSOLE_REQUEST *)(unaligned + 1)) == KCR_INVALID);
    assert(kernel_console_requests_take(&queue,
        (KERNEL_CONSOLE_REQUEST *)(UINTPTR_MAX - 3)) == KCR_INVALID);
    assert(kernel_console_requests_push(&queue, KCR_WRITE, bytes, 257, 257) == KCR_INVALID);
    assert(kernel_console_requests_push(&queue, KCR_WRITE, bytes, 256, 255) == KCR_INVALID);
    assert(kernel_console_requests_push(&queue, KCR_WRITE, NULL, 1, 1) == KCR_INVALID);
    assert(kernel_console_requests_push(&queue, KCR_WRITE,
        (const void *)(UINTPTR_MAX - 1), 4, 4) == KCR_INVALID);
    assert(kernel_console_requests_push(&queue, KCR_WRITE, &queue, 1, 1) == KCR_INVALID);
    assert(kernel_console_requests_push(&queue, 99, bytes, 1, 1) == KCR_INVALID);
    assert(kernel_console_requests_push(&queue, KCR_SUBMIT, bytes, 1, 1) == KCR_INVALID);
    assert(kernel_console_requests_push(&queue, KCR_EDIT, "\n", 1, 1) == KCR_INVALID);
    assert(kernel_console_requests_push(&queue, KCR_WRITE, "\xc0\x80", 2, 2) == KCR_INVALID);
    assert(kernel_console_requests_push(&queue, KCR_WRITE, bytes, 256, 256) == KCR_OK);
    memset(bytes, 'z', sizeof(bytes));
    assert(kernel_console_requests_take(&queue, &output) == KCR_OK);
    assert(output.Operation == KCR_WRITE && output.Length == 256);
    for (uint32_t index = 0; index < 256; ++index) assert(output.Text[index] == 'a');
    assert(kernel_console_requests_take(&queue, &output) == KCR_IN_FLIGHT);
    assert(kernel_console_requests_complete(&queue, 2) == KCR_INVALID);
    assert(kernel_console_requests_complete(&queue, 1) == KCR_OK);
    assert(kernel_console_requests_complete(&queue, 1) == KCR_IN_FLIGHT);
    for (uint32_t index = 0; index < 8; ++index)
        assert(kernel_console_requests_push(&queue, KCR_WRITE, &bytes[index], 1, 1) == KCR_OK);
    assert(kernel_console_requests_push(&queue, KCR_SUBMIT, NULL, 0, 0) == KCR_FULL);
    assert(kernel_console_requests_drain(&queue) == KCR_OK);
    assert(kernel_console_requests_close(&queue) == KCR_PENDING);
    assert(kernel_console_requests_push(&queue, KCR_SUBMIT, NULL, 0, 0) == KCR_STATE);
    for (uint32_t index = 0; index < 8; ++index) {
        assert(kernel_console_requests_take(&queue, &output) == KCR_OK);
        assert(kernel_console_requests_close(&queue) == KCR_IN_FLIGHT);
        assert(kernel_console_requests_complete(&queue, 1) == KCR_OK);
    }
    assert(kernel_console_requests_close(&queue) == KCR_OK);
    empty_info(&queue, KCR_CLOSED);
    assert(kernel_console_requests_take(&queue, &output) == KCR_STATE);
    info = (KERNEL_CONSOLE_REQUEST_INFO){99, 99};
    requests_context_disable();
    assert(kernel_console_requests_query(&queue, &info) == KCR_CONTEXT);
    assert(info.State == 99 && info.Pending == 99 && ks_irq_state() == 0x200);
    prepared = 1;
}

// ====== failures_and_lock_order ======

static void failures_and_lock_order(void) {
    KERNEL_CONSOLE_REQUEST_QUEUE queue = {0};
    KERNEL_CONSOLE_REQUEST output;
    KERNEL_LOCK other = {0};
    KERNEL_LOCK_GUARD guard = {0};
    KERNEL_CONSOLE_REQUEST_INFO info = {0};
    assert(kernel_console_requests_initialize(&queue));
    assert(kernel_lock_initialize(&other, 2));
    assert(kernel_lock_acquire(&other, &context, &guard));
    assert(kernel_console_requests_push(&queue, KCR_SUBMIT, NULL, 0, 0) == KCR_CONTEXT);
    assert(context.Depth == 1 && !ks_irq_state());
    assert(kernel_lock_release(&context, &guard));
    test_set_if(0);
    assert(kernel_console_requests_push(&queue, KCR_SUBMIT, NULL, 0, 0) == KCR_OK);
    assert(!ks_irq_state() && !context.Depth);
    test_set_if(0x200);
    assert(kernel_console_requests_push(&queue, KCR_WRITE, "retained", 8, 8) == KCR_OK);
    assert(kernel_console_requests_take(&queue, &output) == KCR_OK);
    assert(kernel_console_requests_complete(&queue, 0) == KCR_OK);
    assert(kernel_console_requests_query(&queue, &info) == KCR_OK);
    assert(info.State == KCR_FAILED && info.Pending == 1);
    assert(kernel_console_requests_complete(&queue, 1) == KCR_STATE);
    assert(kernel_console_requests_close(&queue) == KCR_STATE);
    assert(!kernel_console_requests_initialize(&queue));
}

// ====== service_consumer ======

static void service_consumer(void) {
    static KERNEL_CONSOLE_SERVICE service;
    static KERNEL_VIDEO_CONSOLE view;
    KERNEL_CONSOLE_REQUEST_QUEUE queue = {0};
    KERNEL_CONSOLE_REQUEST_INFO info;
    assert(kernel_console_requests_initialize(&queue));
    assert(kernel_console_service_init(&service, &view, 40, 8));
    assert(kernel_console_requests_consume(&queue, NULL) == KCR_INVALID);
    assert(kernel_console_requests_consume(&queue, &service) == KCR_EMPTY);
    assert(kernel_console_requests_push(&queue, KCR_EDIT, "ciao", 4, 4) == KCR_OK);
    assert(service.Length == 0);
    assert(kernel_console_requests_consume(&queue, &service) == KCR_OK);
    assert(service.Length == 4 && !context.Depth && ks_irq_state() == 0x200);
    assert(kernel_console_requests_push(&queue, KCR_WRITE, "risposta\n", 9, 9) == KCR_OK);
    assert(kernel_console_requests_consume(&queue, &service) == KCR_OK && service.Length == 4);
    assert(kernel_console_requests_push(&queue, KCR_SUBMIT, NULL, 0, 0) == KCR_OK);
    assert(kernel_console_requests_consume(&queue, &service) == KCR_OK && service.Length == 0);
    assert(kernel_console_requests_push(&queue, KCR_WRITE, "fail", 4, 4) == KCR_OK);
    service.Failed = 1;
    assert(kernel_console_requests_consume(&queue, &service) == KCR_STATE);
    assert(kernel_console_requests_query(&queue, &info) == KCR_OK && info.State == KCR_FAILED);
    assert(kernel_console_requests_consume(&queue, &service) == KCR_STATE);
}

// ====== channel_boundary ======

static void channel_boundary(void) {
    assert(!kernel_console_ap_channel(0,1,17,0));
    assert(!kernel_console_ap_channel(0,0,0,0));
    assert(kernel_console_ap_channel(0,0,17,0) == 1);
    assert(!kernel_console_ap_channel(0,0,18,0));
    assert(!kernel_console_ap_channel(1,1,18,0));
    assert(!kernel_console_ap_channel(1,1,17,2));
    assert(kernel_console_ap_channel(1,1,17,0) == 1);
    assert(kernel_console_ap_channel(2,1,17,0) == 1);
    assert(!kernel_console_ap_channel(2,1,17,0));
    assert(!kernel_console_ap_channel(4,0,17,0));
    assert(kernel_console_ap_channel(2,1,17,1) == 1);
    assert(kernel_console_ap_channel(3,0,17,0) == 3);
    assert(kernel_console_ap_channel(4,0,17,0) == 1);
    assert(kernel_console_ap_channel(3,0,17,0) == 7);
    assert(!kernel_console_ap_channel(1,1,17,0));
    assert(!kernel_console_ap_channel(99,0,17,0));
}

// ====== main ======

int main(void) {
    wait = (KERNEL_LOCK_WAIT){{test_clock, NULL, UINT32_MAX, 1000}, 10000, 100000000};
    requests_context_prepare();
    channel_boundary();
    boundaries();
    failures_and_lock_order();
    service_consumer();
    requests_threads();
    puts("PASS: console request C/Rust ABI, ownership, bounds, real locks, failure and IF restore; IRQ/context are MOCK");
    return 0;
}
