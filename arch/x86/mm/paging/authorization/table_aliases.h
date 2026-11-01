#ifndef OS_X86_PAGING_TABLE_ALIASES_H
#define OS_X86_PAGING_TABLE_ALIASES_H
#include "../inventory.h"

// ====== x86_paging_leaf_aliases_absent ======

static __declspec(noinline) int x86_paging_leaf_aliases_absent(const X86_PAGING_INVENTORY *draft, BOOT_U64 mask) {
    BOOT_U64 frames[X86_BOOT_TABLE_LIMIT];
    if (!draft || !draft->Count || draft->Count > X86_BOOT_TABLE_LIMIT) return 0;
    for (BOOT_U32 index = 0; index < draft->Count; ++index) {
        BOOT_U64 physical = draft->Tables[index].Physical;
        BOOT_U32 position = index;
        while (position && frames[position - 1] > physical) {
            frames[position] = frames[position - 1];
            --position;
        }
        frames[position] = physical;
    }
    for (BOOT_U32 index = 0; index < draft->Count; ++index) {
        if (draft->Tables[index].Level != 3) continue;
        const volatile BOOT_U64 *entries = x86_paging_view(draft->Tables[index].View);
        if (!entries) return 0;
        for (BOOT_U32 slot = 0; slot < 512; ++slot) {
            BOOT_U64 entry = entries[slot];
            if (!entry) continue;
            BOOT_U64 physical = entry & mask;
            BOOT_U32 first = 0, limit = draft->Count;
            while (first < limit) {
                BOOT_U32 middle = first + (limit - first) / 2;
                if (frames[middle] < physical) first = middle + 1;
                else limit = middle;
            }
            if (first < draft->Count && frames[first] == physical) return 0;
        }
    }
    return 1;
}
#endif
