#include "lapic.h"

// ====== lettura dword solo nella pagina LAPIC dedicata ===== //

int x86_lapic_native_read(BOOT_U32 offset, BOOT_U32 *value)
{
    *value = *(volatile BOOT_U32 *)(X86_LAPIC_VIRTUAL + offset);
    return 1;
}

// ====== EOI zero senza modificare la finestra RAM temporanea ===== //

int x86_lapic_native_eoi(void)
{
    *(volatile BOOT_U32 *)(X86_LAPIC_VIRTUAL + 0xb0) = 0;
    return 1;
}
