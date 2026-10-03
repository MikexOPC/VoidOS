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
    return (volatile unsigned char *)x86_ist_base(stack);
}

// ====== INVLPG locale nel profilo senza SMP e PCID ===== //

void x86_stack_invalidate(BOOT_U64 address)
{
    x86_invalidate_page(address);
}
