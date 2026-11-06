#ifndef KERNEL_CONSOLE_REQUEST_QUEUE_H
#define KERNEL_CONSOLE_REQUEST_QUEUE_H
#include "kernel/sync/lock.h"
#include "../service/console.h"

#define KCR_STORAGE_BYTES 2560U
#define KCR_TEXT_LIMIT 256U
#define KCR_WRITE 1U
#define KCR_EDIT 2U
#define KCR_SUBMIT 3U
#define KCR_OK 0U
#define KCR_INVALID 1U
#define KCR_FULL 2U
#define KCR_EMPTY 3U
#define KCR_STATE 4U
#define KCR_IN_FLIGHT 5U
#define KCR_PENDING 6U
#define KCR_CONTEXT 7U
#define KCR_OPEN 1U
#define KCR_DRAINING 2U
#define KCR_CLOSED 3U
#define KCR_FAILED 4U

typedef struct {
    uint32_t  Operation, Length;
    uint8_t   Text[KCR_TEXT_LIMIT];
} KERNEL_CONSOLE_REQUEST;

typedef struct {
    uint32_t  State, Pending;
} KERNEL_CONSOLE_REQUEST_INFO;

typedef struct {
    uintptr_t            Self;
    KERNEL_LOCK          Lock;
    _Alignas(8) uint8_t  Storage[KCR_STORAGE_BYTES];
} KERNEL_CONSOLE_REQUEST_QUEUE;

_Static_assert(sizeof(KERNEL_CONSOLE_REQUEST) == 264, "Console request ABI");
_Static_assert(offsetof(KERNEL_CONSOLE_REQUEST, Text) == 8, "Console payload ABI");
_Static_assert(offsetof(KERNEL_CONSOLE_REQUEST_QUEUE, Storage) % 8 == 0, "Queue storage alignment");

int kernel_console_requests_initialize(KERNEL_CONSOLE_REQUEST_QUEUE *queue);
uint32_t kernel_console_requests_push(KERNEL_CONSOLE_REQUEST_QUEUE *queue,
    uint32_t operation, const void *text, uint32_t length, uint32_t capacity);
uint32_t kernel_console_requests_take(KERNEL_CONSOLE_REQUEST_QUEUE *queue, KERNEL_CONSOLE_REQUEST *output);
uint32_t kernel_console_requests_complete(KERNEL_CONSOLE_REQUEST_QUEUE *queue, uint32_t success);
uint32_t kernel_console_requests_drain(KERNEL_CONSOLE_REQUEST_QUEUE *queue);
uint32_t kernel_console_requests_close(KERNEL_CONSOLE_REQUEST_QUEUE *queue);
uint32_t kernel_console_requests_query(KERNEL_CONSOLE_REQUEST_QUEUE *queue, KERNEL_CONSOLE_REQUEST_INFO *output);
uint32_t kernel_console_requests_consume(KERNEL_CONSOLE_REQUEST_QUEUE *queue, KERNEL_CONSOLE_SERVICE *service);
__declspec(noreturn) void kernel_console_requests_fail_stop(void);
#endif
