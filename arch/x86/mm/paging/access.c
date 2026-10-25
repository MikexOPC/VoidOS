#include "inventory.h"
#include "arch/x86/configs/layout.h"

// ====== accesso esclusivamente in lettura alla finestra ricorsiva del PML4 ===== //

const volatile BOOT_U64 *x86_paging_view(BOOT_U64 address)
{
    if ((address & 4095) || X86_LAYOUT_SLOT_OF(address) != X86_BOOT_RECURSIVE_SLOT) return 0;
    return (const volatile BOOT_U64 *)address;
}
