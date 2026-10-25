#include "access.h"
#include "arch/x86/configs/layout.h"

// ====== viste ricorsive dei quattro livelli per la sola finestra iniziale ====== //

volatile BOOT_U64 *x86_window_entries(BOOT_U32 level)
{
    BOOT_U64 r = X86_BOOT_RECURSIVE_SLOT, v = X86_KERNEL_PAGE_WINDOW;
    BOOT_U64 a = (v >> 39) & 511, b = (v >> 30) & 511, c = (v >> 21) & 511;
    BOOT_U64 address;
    if (level == 0) address = X86_LAYOUT_RECURSIVE_VIEW(r, r, r, r);
    else if (level == 1) address = X86_LAYOUT_RECURSIVE_VIEW(r, r, r, a);
    else if (level == 2) address = X86_LAYOUT_RECURSIVE_VIEW(r, r, a, b);
    else if (level == 3) address = X86_LAYOUT_RECURSIVE_VIEW(r, a, b, c);
    else return 0;
    return (volatile BOOT_U64 *)address;
}

// ====== memoria dati raggiungibile solo dopo pubblicazione e invalidazione ====== //

volatile unsigned char *x86_window_bytes(void)
{
    return (volatile unsigned char *)X86_KERNEL_PAGE_WINDOW;
}
