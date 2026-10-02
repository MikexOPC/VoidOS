#include "access.h"

// ====== viste ricorsive dei quattro livelli per la sola finestra iniziale ====== //

volatile BOOT_U64 *x86_window_entries(BOOT_U32 level)
{
    BOOT_U64 r = X86_BOOT_RECURSIVE_SLOT, v = X86_KERNEL_PAGE_WINDOW;
    BOOT_U64 a = (v >> 39) & 511, b = (v >> 30) & 511, c = (v >> 21) & 511;
    BOOT_U64 address;
    if (level == 0) address = (r << 39) | (r << 30) | (r << 21) | (r << 12);
    else if (level == 1) address = (r << 39) | (r << 30) | (r << 21) | (a << 12);
    else if (level == 2) address = (r << 39) | (r << 30) | (a << 21) | (b << 12);
    else if (level == 3) address = (r << 39) | (a << 30) | (b << 21) | (c << 12);
    else return 0;
    return (volatile BOOT_U64 *)address;
}

// ====== memoria dati raggiungibile solo dopo pubblicazione e invalidazione ====== //

volatile unsigned char *x86_window_bytes(void)
{
    return (volatile unsigned char *)X86_KERNEL_PAGE_WINDOW;
}
