#include "retire.h"
#include "arch/x86/configs/layout.h"
#include "arch/x86/kernel/descriptors/stacks.h"

// ====== x86_retire_cpu_valid ======

// Intel SDM 090US 2.5, 5.10.4.1/2: CR4.PGE7 and PCIDE17 must be zero for the CR3 flush profile.
int x86_retire_cpu_valid(const X86_RETIRE_CPU *cpu, BOOT_U64 root) {
    if (!cpu ||
        !root ||
        (root & 4095) ||
        root >= (1ULL << 52) ||
        cpu->Root != root ||
        (cpu->Cr0 & 0x80010000ULL) != 0x80010000ULL ||
        !(cpu->Cr4 & (1ULL << 5)) ||
        (cpu->Cr4 & ((1ULL << 7) | (1ULL << 12) | (1ULL << 17))) ||
        (cpu->Efer & 0xd00) != 0xd00 ||
        !(cpu->Flags & 2) ||
        (cpu->Flags & ((1ULL << 9) | (3ULL << 12) | (1ULL << 14) | (1ULL << 17))) ||
        cpu->Tr != 24 ||
        cpu->Cs != 8 ||
        cpu->Ss != 16 ||
        cpu->Ldt ||
        !X86_KERNEL_IMAGE_CONTAINS(cpu->Rip, 1) ||
        cpu->Rsp < X86_KERNEL_MAIN_BASE ||
        cpu->Rsp >= X86_KERNEL_MAIN_TOP ||
        cpu->GdtLimit != 55 ||
        cpu->IdtLimit != 511 ||
        !X86_KERNEL_IMAGE_CONTAINS(cpu->GdtBase, 56) ||
        !X86_KERNEL_IMAGE_CONTAINS(cpu->IdtBase, 512)
    )
        return 0;
    return 1;
}
