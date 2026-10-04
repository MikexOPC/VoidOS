#ifndef OS_X86_UART_IRQ_ROUTE_H
#define OS_X86_UART_IRQ_ROUTE_H
#include "arch/x86/kernel/interrupts/apic/control/masked.h"
typedef struct { BOOT_U32 Io, Pin, Gsi, Bits, Destination, Reserved; } X86_UART_ROUTE;
typedef struct {
    volatile BOOT_U32 Count, Error;
    X86_PAGE_WINDOW *Window;
    const X86_APIC_PROFILE *Profile;
    BOOT_U32 Active, Reserved;
} X86_UART_IRQ_OWNER;
_Static_assert(sizeof(X86_UART_IRQ_OWNER) == 32, "Stato UART IRQ bounded");
_Static_assert(sizeof(X86_UART_ROUTE) == 24, "Rotta UART bounded");
_Static_assert(offsetof(X86_UART_IRQ_OWNER, Count) == 0 &&
    offsetof(X86_UART_IRQ_OWNER, Error) == 4, "ABI attesa UART");
BOOT_U32 x86_uart_route_bits(BOOT_U32 flags);
BOOT_U32 x86_uart_thre(BOOT_U32 iir);
int x86_uart_route_plan(const KERNEL_MADT_TOPOLOGY *topology, const X86_APIC_CAPABILITIES *caps,
    X86_UART_ROUTE *output);
int x86_uart_route_apply(X86_PAGE_WINDOW *window, const X86_APIC_PROFILE *profile,
    const X86_APIC_CAPABILITIES *caps, const X86_UART_ROUTE *route, BOOT_U32 masked);
int x86_uart_eoi(X86_PAGE_WINDOW *window, const X86_APIC_PROFILE *profile);
int x86_uart_irq_run(const KERNEL_BOOT_CONTEXT *context, X86_PAGE_WINDOW *window,
    const X86_PAGING_INVENTORY *inventory, const KERNEL_MADT_TOPOLOGY *topology,
    const X86_APIC_PROFILE *profile, const X86_APIC_CAPABILITIES *caps);
int x86_uart_irq_handle(void);
int x86_uart_irq_service(X86_UART_IRQ_OWNER *owner);
int x86_uart_lapic_eoi_zero(void);
int x86_uart_irq_wait(volatile BOOT_U32 *counts, BOOT_U32 goal, BOOT_U32 budget);
#endif
