#include "gdt.h"
#include "arch/x86/configs/layout.h"
#include "lib/memory/address_ranges.h"

// ====== profilo flat ring zero con bit accessed gia impostato ====== //

static uint64_t segment(uint8_t access, uint8_t flags)
{
    return UINT64_C(0xffff) | ((uint64_t)access << 40) |
           (UINT64_C(15) << 48) | ((uint64_t)flags << 52);
}

// ====== limiti del profilo a quattro livelli e separazione degli output ====== //

static int valid_arguments(const X86_BOOT_GDT *gdt, const X86_GDT_POINTER *pointer, uint64_t base)
{
    // K1: one canonical half, either one (layout.h).
    if (!gdt || !pointer || (base & 7) || !X86_LAYOUT_RANGE(base, sizeof(*gdt)))
        return 0;
    return !os_address_ranges_overlap((uint64_t)(uintptr_t)gdt, sizeof(*gdt),
                                      (uint64_t)(uintptr_t)pointer, sizeof(*pointer));
}

// ====== codifica little endian senza bitfield o accessi non allineati ====== //

int x86_prepare_boot_gdt(X86_BOOT_GDT *gdt, X86_GDT_POINTER *pointer, uint64_t linear_base)
{
    uint32_t index;
    if (!valid_arguments(gdt, pointer, linear_base))
        return 0;
    gdt->Entries[0] = 0;
    /* Codice: P=1, DPL=0, S=1, execute/read/accessed, L=1, D=0.
       Dati: P=1, DPL=0, S=1, read/write/accessed, L=0, B=1. */
    gdt->Entries[1] = segment(0x9b, 0x0a);
    gdt->Entries[2] = segment(0x93, 0x0c);
    pointer->Bytes[0] = (uint8_t)(sizeof(*gdt) - 1);
    pointer->Bytes[1] = 0;
    for (index = 0; index < 8; ++index)
        pointer->Bytes[index + 2] = (uint8_t)(linear_base >> (index * 8));
    return x86_validate_boot_gdt(gdt, pointer, linear_base);
}

// ====== verifica esatta prima del futuro caricamento privilegiato ====== //

int x86_validate_boot_gdt(const X86_BOOT_GDT *gdt, const X86_GDT_POINTER *pointer, uint64_t linear_base)
{
    uint64_t encoded_base = 0;
    uint32_t index;
    if (!valid_arguments(gdt, pointer, linear_base))
        return 0;
    if (pointer->Bytes[0] != sizeof(*gdt) - 1 || pointer->Bytes[1])
        return 0;
    for (index = 0; index < 8; ++index)
        encoded_base |= (uint64_t)pointer->Bytes[index + 2] << (index * 8);
    return encoded_base == linear_base && !gdt->Entries[0] &&
           gdt->Entries[1] == segment(0x9b, 0x0a) &&
           gdt->Entries[2] == segment(0x93, 0x0c);
}
