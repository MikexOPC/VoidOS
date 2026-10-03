#include "mapping.h"
#include "arch/x86/mm/window/access.h"

// ====== ramo condiviso senza duplicare il calcolo della vista ricorsiva ===== //

volatile BOOT_U64 *x86_region_entries(BOOT_U32 level)
{
    return x86_window_entries(level);
}

// ====== byte raggiungibili soltanto durante il prestito vivo del mapping ===== //

volatile unsigned char *x86_region_bytes(BOOT_U32 slot)
{
    BOOT_U64 base = x86_region_base(slot);
    return base ? (volatile unsigned char *)base : 0;
}

// ====== riuso del backend INVLPG locale dopo il batch di PTE ===== //

void x86_region_invalidate(BOOT_U64 address)
{
    x86_invalidate_page(address);
}
