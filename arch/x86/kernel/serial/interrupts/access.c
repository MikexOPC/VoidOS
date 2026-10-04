#include "route.h"

// ====== sola scrittura EOI zero nel prestito LAPIC gia verificato ===== //

int x86_uart_lapic_eoi_zero(void)
{
    *(volatile BOOT_U32 *)(X86_KERNEL_PAGE_WINDOW + 0xb0) = 0;
    return 1;
}
