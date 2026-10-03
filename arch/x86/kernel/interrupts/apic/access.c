#include "probe.h"

// ====== sole letture dword dei registri previsti dal probe ===== //

int x86_apic_mmio_read(BOOT_U32 offset, BOOT_U32 *output)
{
    if (!output || (offset != 0 && offset != 0x10 && offset != 0x20 && offset != 0x30)) return 0;
    *output = *(volatile BOOT_U32 *)(X86_KERNEL_PAGE_WINDOW + offset);
    return 1;
}

// ====== sola scrittura del selettore nessuna scrittura IOWIN o redirection ===== //

int x86_apic_mmio_select(BOOT_U32 value)
{
    if (value > 255) return 0;
    *(volatile BOOT_U32 *)X86_KERNEL_PAGE_WINDOW = value;
    return 1;
}
