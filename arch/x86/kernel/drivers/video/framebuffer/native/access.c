#include "../mapping.h"
#include "arch/x86/mm/window/access.h"
#include <intrin.h>

// ====== x86_fb_cpu ======

int x86_fb_cpu(X86_FB_CPU *output)
{
    X86_FB_CPU draft = {0};
    int registers[4];
    BOOT_U64 cr0, cr4, flags;

    if (!output || ((BOOT_U64)output & 7)) return 0;
    __cpuid(registers, 0);

    if ((unsigned int)registers[0] < 1) return 0;
    __cpuid(registers, 1);

    if (((unsigned int)registers[3] & ((1U << 5) | (1U << 16))) != ((1U << 5) | (1U << 16))) return 0;
    __cpuid(registers, (int)0x80000000U);

    if ((unsigned int)registers[0] < 0x80000008U) return 0;
    __cpuid(registers, (int)0x80000001U);

    if (!((unsigned int)registers[3] & (1U << 20))) return 0;
    cr0 = __readcr0(); cr4 = __readcr4(); flags = __readeflags();

    if ((flags & 0x202) != 2 || (cr0 & ((1ULL << 31) | (1ULL << 16) | 1)) !=
        ((1ULL << 31) | (1ULL << 16) | 1) || (cr0 & ((1ULL << 29) | (1ULL << 30))) ||
        !(cr4 & (1ULL << 5)) || (cr4 & ((1ULL << 12) | (1ULL << 17))) ||
        (__readmsr(0xc0000080) & 0xc00) != 0xc00) return 0;
    __cpuid(registers, (int)0x80000008U);

    draft.PhysicalBits = (unsigned int)registers[0] & 255;

    if (draft.PhysicalBits < 32 || draft.PhysicalBits > 52) return 0;
    draft.Root = __readcr3(); draft.Pat = __readmsr(0x277);

    if (!draft.Root || (draft.Root & 4095) || draft.Root >= (1ULL << draft.PhysicalBits)) return 0;
    *output = draft;

    return 1;
}

// ====== x86_fb_root ======

volatile BOOT_U64 *x86_fb_root(void)
{
    return x86_window_entries(0);
}

// ====== x86_fb_flush ======

int x86_fb_flush(BOOT_U64 root)
{
    X86_FB_CPU cpu;
    if (!x86_fb_cpu(&cpu) || cpu.Root != root) return 0;
    __writecr3(root);
    // H2: a CR3 reload publishes PML4 changes; it starts a new paging generation as well.
    x86_paging_generation_bump();
    return __readcr3() == root;
}
