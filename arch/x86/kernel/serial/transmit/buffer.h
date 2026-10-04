#ifndef OS_X86_UART_TX_BUFFER_H
#define OS_X86_UART_TX_BUFFER_H
#include "arch/x86/kernel/platform.h"
#define X86_UART_TX_CAPACITY 64U
typedef struct {
    BOOT_U64 Self;
    BOOT_U32 Length;
    volatile BOOT_U32 Next, Done;
    BOOT_U32 Reserved;
    unsigned char Data[X86_UART_TX_CAPACITY];
} X86_UART_TX;
_Static_assert(sizeof(X86_UART_TX) == 88, "Buffer UART TX con dati posseduti");
BOOT_U32 x86_uart_tx_bounds(BOOT_U32 length, BOOT_U32 next, BOOT_U32 done);
BOOT_U32 x86_uart_tx_goal(BOOT_U32 base, BOOT_U32 length);
int x86_uart_tx_validate(const X86_UART_TX *buffer);
int x86_uart_tx_progress(const X86_UART_TX *buffer, BOOT_U32 deliveries);
int x86_uart_tx_prepare(X86_UART_TX *buffer, const unsigned char *source, BOOT_U32 bytes);
int x86_uart_tx_start(const X86_UART_TX *buffer);
int x86_uart_tx_service(X86_UART_TX *buffer);
int x86_uart_tx_drain(const X86_UART_TX *buffer);
int x86_uart_tx_release(X86_UART_TX *buffer, BOOT_U32 deliveries);
#endif
