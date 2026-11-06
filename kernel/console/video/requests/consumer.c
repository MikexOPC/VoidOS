#include "queue.h"
#include "../service/console.h"

// ====== kernel_console_requests_consume ======

uint32_t kernel_console_requests_consume(KERNEL_CONSOLE_REQUEST_QUEUE *queue,
    KERNEL_CONSOLE_SERVICE *service) {
    KERNEL_CONSOLE_REQUEST packet;
    uint32_t result;
    int success;
    if (!service) return KCR_INVALID;
    result = kernel_console_requests_take(queue, &packet);
    if (result != KCR_OK) return result;
    switch (packet.Operation) {
        case KCR_WRITE:
            success = kernel_console_service_write(service, (const char *)packet.Text,
                packet.Length, sizeof(packet.Text));
            break;
        case KCR_EDIT:
            success = kernel_console_service_edit(service, (const char *)packet.Text,
                packet.Length, sizeof(packet.Text));
            break;
        case KCR_SUBMIT:
            success = kernel_console_service_submit(service);
            break;
        default: success = 0; break;
    }
    result = kernel_console_requests_complete(queue, (uint32_t)success);
    return result == KCR_OK && !success ? KCR_STATE : result;
}
