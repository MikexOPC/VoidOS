#ifndef OS_X86_UART_QUEUE_H
#define OS_X86_UART_QUEUE_H
#include "arch/x86/kernel/serial/transmit/buffer.h"
#define X86_UART_QUEUE_CAPACITY 3U
enum {
    X86_UART_QUEUE_INVALID = 0,
    X86_UART_QUEUE_OK = 1,
    X86_UART_QUEUE_FULL = 2,
    X86_UART_QUEUE_EXHAUSTED = 3
};
typedef struct {
    BOOT_U64 Id;
    BOOT_U32 Length, Reserved;
    unsigned char Data[X86_UART_TX_CAPACITY];
} X86_UART_PACKET;
typedef struct {
    BOOT_U64 Self, Next, Completed;
    BOOT_U32 Head, Count;
    X86_UART_PACKET Slots[X86_UART_QUEUE_CAPACITY];
} X86_UART_QUEUE;
_Static_assert(sizeof(X86_UART_PACKET) == 80, "Pacchetto TX posseduto");
_Static_assert(sizeof(X86_UART_QUEUE) == 272, "Coda TX senza allocazioni");
BOOT_U32 x86_uart_queue_state(BOOT_U32 head, BOOT_U32 count, BOOT_U64 next, BOOT_U64 completed);
BOOT_U32 x86_uart_queue_slot(BOOT_U32 head, BOOT_U32 ordinal);
int x86_uart_queue_initialize(X86_UART_QUEUE *queue);
int x86_uart_queue_validate(const X86_UART_QUEUE *queue);
int x86_uart_queue_submit(X86_UART_QUEUE *queue, const unsigned char *source, BOOT_U32 bytes, BOOT_U64 *receipt);
int x86_uart_queue_front(const X86_UART_QUEUE *queue, X86_UART_PACKET *output);
// Solo dopo TX, EOI e TEMT verificati; un errore parziale non autorizza un retry.
int x86_uart_queue_complete(X86_UART_QUEUE *queue, BOOT_U64 id);
#endif
