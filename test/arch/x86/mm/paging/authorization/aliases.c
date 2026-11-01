#include <stdio.h>
#include <string.h>
#include "arch/x86/mm/paging/authorization/table_aliases.h"
static BOOT_U64 alias_pages[128][512];
static X86_PAGING_INVENTORY alias_model, copy;
static unsigned missing;
#define TEST_MASK 0x000ffffffffff000ULL

// ====== x86_paging_view ======

const volatile BOOT_U64 *x86_paging_view(BOOT_U64 view)
{
    return missing || !view || view > 128 ? 0 : alias_pages[view - 1];
}

// ====== reference ======

static int reference(void)
{
    for (BOOT_U32 i = 0; i < alias_model.Count; ++i) {
        if (alias_model.Tables[i].Level != 3) continue;
        const volatile BOOT_U64 *page = x86_paging_view(alias_model.Tables[i].View);
        if (!page) return 0;
        for (BOOT_U32 slot = 0; slot < 512; ++slot) {
            BOOT_U64 entry = page[slot];
            if (!entry) continue;
            for (BOOT_U32 table = 0; table < alias_model.Count; ++table)
                if ((entry & TEST_MASK) == alias_model.Tables[table].Physical) return 0;
        }
    }
    return 1;
}

// ====== run_alias_differential ======

int run_alias_differential(void)
{
    unsigned cases = 0;
    if (x86_paging_leaf_aliases_absent(0, TEST_MASK) ||
        x86_paging_leaf_aliases_absent(&alias_model, TEST_MASK)) return 1;
    alias_model.Count = 129;
    if (x86_paging_leaf_aliases_absent(&alias_model, TEST_MASK)) return 2;
    for (unsigned count = 1; count <= 128; ++count) {
        memset(&alias_model, 0, sizeof(alias_model)); memset(alias_pages, 0, sizeof(alias_pages));
        alias_model.Count = count;
        for (unsigned i = 0; i < count; ++i) {
            alias_model.Tables[i].Physical = (BOOT_U64)(count - i) * 4096;
            alias_model.Tables[i].Level = i == count - 1 ? 3 : 1;
            alias_model.Tables[i].View = i + 1;
        }
        for (unsigned slot = 0; slot < 512; ++slot)
            alias_pages[count - 1][slot] = 0x8000000000200063ULL + (BOOT_U64)slot * 4096;
        copy = alias_model;
        if (!reference() || !x86_paging_leaf_aliases_absent(&alias_model, TEST_MASK) ||
            memcmp(&copy, &alias_model, sizeof(alias_model))) return 3;
        ++cases;
        for (unsigned table = 0; table < count; ++table) {
            unsigned slot = (table * 251) % 512;
            BOOT_U64 saved = alias_pages[count - 1][slot];
            alias_pages[count - 1][slot] = alias_model.Tables[table].Physical | 0x8000000000000063ULL;
            if (reference() != x86_paging_leaf_aliases_absent(&alias_model, TEST_MASK) || reference()) return 4;
            alias_pages[count - 1][slot] = saved; ++cases;
        }
        missing = 1;
        if (reference() || x86_paging_leaf_aliases_absent(&alias_model, TEST_MASK)) return 5;
        missing = 0; ++cases;
    }
    printf("PASS alias differential: %u cases\n", cases);
    return 0;
}
