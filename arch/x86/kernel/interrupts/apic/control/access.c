#include "masked.h"

// ====== letture dword bounded nel prestito dispositivo interno ===== //

int x86_apic_control_read(BOOT_U32 offset, BOOT_U32 *output)
{
    if (!output || (offset & 15) || offset > 0x370) return 0;
    *output = *(volatile BOOT_U32 *)(X86_KERNEL_PAGE_WINDOW + offset);
    return 1;
}

// ====== scritture soltanto su registri configurati dal controller mascherato ===== //

int x86_apic_control_write(BOOT_U32 offset, BOOT_U32 value)
{
    if (offset != 0 && offset != 0x10 && offset != 0x80 && offset != 0xf0 &&
        offset != 0x2f0 && (offset < 0x320 || offset > 0x370 || (offset & 15))) return 0;
    *(volatile BOOT_U32 *)(X86_KERNEL_PAGE_WINDOW + offset) = value;
    return 1;
}
