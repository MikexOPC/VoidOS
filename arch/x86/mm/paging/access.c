#include "inventory.h"
#include "arch/x86/configs/layout.h"

// ====== accesso esclusivamente in lettura alla finestra ricorsiva del PML4 ===== //

const volatile BOOT_U64 *x86_paging_view(BOOT_U64 address)
{
    if ((address & 4095) || X86_LAYOUT_SLOT_OF(address) != X86_BOOT_RECURSIVE_SLOT) return 0;
    return (const volatile BOOT_U64 *)address;
}

// ====== x86_paging_lower_slots ======

// K1 criterion 1: present PML4 entries in the lower half (slots 0-255), read through the
// recursive view. first receives the lowest one; UINT32_MAX when the root view is unreadable.
BOOT_U32 x86_paging_lower_slots(BOOT_U32 *first)
{
    const volatile BOOT_U64 *root = x86_paging_view(X86_LAYOUT_RECURSIVE_VIEW(X86_LAYOUT_RECURSIVE_SLOT,
        X86_LAYOUT_RECURSIVE_SLOT, X86_LAYOUT_RECURSIVE_SLOT, X86_LAYOUT_RECURSIVE_SLOT));
    BOOT_U32 slot, count = 0;

    if (!root || !first) return 0xFFFFFFFFU;
    *first = 512;
    for (slot = 0; slot < 256; ++slot) {
        if (!(root[slot] & 1)) continue;
        if (!count) *first = slot;
        ++count;
    }
    return count;
}
