#include "queue.h"
#include "kernel/sync/internal.h"

uint32_t kernel_console_requests_raw(void *storage, size_t capacity,
    uint32_t command, KERNEL_CONSOLE_REQUEST *packet);

// ====== span ======

static int span(const void *pointer, size_t bytes, size_t alignment) {
    return pointer && !((uintptr_t)pointer % alignment) && (uintptr_t)pointer <= UINTPTR_MAX - bytes;
}

// ====== separate ======

static int separate(const void *first, size_t size, const void *second, size_t extent) {
    return (uintptr_t)first + size <= (uintptr_t)second ||
        (uintptr_t)second + extent <= (uintptr_t)first;
}

// ====== valid ======

static int valid(const KERNEL_CONSOLE_REQUEST_QUEUE *queue) {
    return span(queue, sizeof(*queue), _Alignof(KERNEL_CONSOLE_REQUEST_QUEUE)) &&
        queue->Self == (uintptr_t)queue;
}

// ====== apply ======

static uint32_t apply(KERNEL_CONSOLE_REQUEST_QUEUE *queue, uint32_t command,
    KERNEL_CONSOLE_REQUEST *packet, void *output, size_t extent) {
    KERNEL_LOCK_GUARD guard = {0};
    KERNEL_LOCK_CONTEXT *context;
    uint32_t result;
    uint64_t saved_if;
    if (!valid(queue)) return KCR_INVALID;
    saved_if = ks_irq_save();
    context = kernel_lock_current_context();
    if (!context || context->Depth ||
        !separate(queue, sizeof(*queue), context, sizeof(*context))) {
        ks_irq_restore(saved_if);
        return KCR_CONTEXT;
    }
    if (extent && (!span(output, extent, _Alignof(uint32_t)) ||
        !separate(queue, sizeof(*queue), output, extent) ||
        !separate(context, sizeof(*context), output, extent))) {
        ks_irq_restore(saved_if);
        return KCR_INVALID;
    }
    if (!kernel_lock_acquire(&queue->Lock, context, &guard)) {
        ks_irq_restore(saved_if);
        return KCR_CONTEXT;
    }
    result = kernel_console_requests_raw(queue->Storage, sizeof(queue->Storage), command, packet);
    if (!kernel_lock_release(context, &guard)) kernel_console_requests_fail_stop();
    if (result == KCR_OK && command == 2) *(KERNEL_CONSOLE_REQUEST *)output = *packet;
    if (result == KCR_OK && command == 6) {
        KERNEL_CONSOLE_REQUEST_INFO *info = output;
        info->State = packet->Operation;
        info->Pending = packet->Length;
    }
    ks_irq_restore(saved_if);
    return result;
}

// ====== kernel_console_requests_initialize ======

int kernel_console_requests_initialize(KERNEL_CONSOLE_REQUEST_QUEUE *queue) {
    KERNEL_CONSOLE_REQUEST packet = {0};
    if (!span(queue, sizeof(*queue), _Alignof(KERNEL_CONSOLE_REQUEST_QUEUE)) || queue->Self ||
        !kernel_lock_initialize(&queue->Lock, 1)) return 0;
    if (kernel_console_requests_raw(queue->Storage, sizeof(queue->Storage), 0, &packet) != KCR_OK)
        kernel_console_requests_fail_stop();
    queue->Self = (uintptr_t)queue;
    return 1;
}

// ====== kernel_console_requests_push ======

uint32_t kernel_console_requests_push(KERNEL_CONSOLE_REQUEST_QUEUE *queue,
    uint32_t operation, const void *text, uint32_t length, uint32_t capacity) {
    KERNEL_CONSOLE_REQUEST packet = {0};
    if (!valid(queue) || length > KCR_TEXT_LIMIT || capacity < length ||
        (length && (!span(text, length, 1) || !separate(queue, sizeof(*queue), text, length))))
        return KCR_INVALID;
    packet.Operation = operation;
    packet.Length = length;
    for (uint32_t index = 0; index < length; ++index) packet.Text[index] = ((const uint8_t *)text)[index];
    return apply(queue, 1, &packet, NULL, 0);
}

// ====== kernel_console_requests_take ======

uint32_t kernel_console_requests_take(KERNEL_CONSOLE_REQUEST_QUEUE *queue, KERNEL_CONSOLE_REQUEST *output) {
    KERNEL_CONSOLE_REQUEST packet = {0};
    return apply(queue, 2, &packet, output, sizeof(*output));
}

// ====== kernel_console_requests_complete ======

uint32_t kernel_console_requests_complete(KERNEL_CONSOLE_REQUEST_QUEUE *queue, uint32_t success) {
    KERNEL_CONSOLE_REQUEST packet = {0};
    packet.Operation = success;
    return apply(queue, 3, &packet, NULL, 0);
}

// ====== kernel_console_requests_drain ======

uint32_t kernel_console_requests_drain(KERNEL_CONSOLE_REQUEST_QUEUE *queue) {
    KERNEL_CONSOLE_REQUEST packet = {0};
    return apply(queue, 4, &packet, NULL, 0);
}

// ====== kernel_console_requests_close ======

uint32_t kernel_console_requests_close(KERNEL_CONSOLE_REQUEST_QUEUE *queue) {
    KERNEL_CONSOLE_REQUEST packet = {0};
    return apply(queue, 5, &packet, NULL, 0);
}

// ====== kernel_console_requests_query ======

uint32_t kernel_console_requests_query(KERNEL_CONSOLE_REQUEST_QUEUE *queue, KERNEL_CONSOLE_REQUEST_INFO *output) {
    KERNEL_CONSOLE_REQUEST packet = {0};
    return apply(queue, 6, &packet, output, sizeof(*output));
}
