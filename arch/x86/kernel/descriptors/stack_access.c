#include "stacks.h"
#include "arch/x86/mm/window/access.h"

// ====== ramo condiviso con la finestra ma foglie riservate agli IST ===== //

volatile BOOT_U64 *x86_stack_entries(BOOT_U32 level)
{
    return x86_window_entries(level);
}

// ====== byte raggiungibili solo dopo preparazione delle pagine ===== //

volatile unsigned char *x86_stack_bytes(BOOT_U32 stack)
{
    if (stack == 5) return (volatile unsigned char *)X86_KERNEL_SYSCALL_BASE;
    return (volatile unsigned char *)(stack == 4 ? X86_KERNEL_MAIN_BASE : x86_ist_base(stack));
}

// ====== INVLPG locale nel profilo senza SMP e PCID ===== //

void x86_stack_invalidate(BOOT_U64 address)
{
    x86_invalidate_page(address);
}
