#include "arch/x86/kernel/interrupts/return/table.h"
#define T 0x100000000ULL
#define T8 T,T,T,T,T,T,T,T
const BOOT_U64 x86_kernel_fault_targets[32] = {T8,T8,T8,T8};
const BOOT_U64 x86_irq_targets[224] = {0};
const unsigned char x86_irq_begin[1] = {0}, x86_irq_end[1] = {0};
static X86_IRQ_IDT table;
static X86_KERNEL_IDT exceptions;
static X86_KERNEL_IDTR pointer, active;
static BOOT_U64 targets[224];
static BOOT_U32 environment, loads, fault;

// ====== SIDT LIDT e stato CPU sostituiti da mock bounded ===== //

void x86_exception_read_idt(X86_KERNEL_IDTR *output) { *output = active; }
void x86_exception_load_idt(const X86_KERNEL_IDTR *input)
{
    active = *input;
    if ((fault == 1 && !loads) || fault == 2) active.Bytes[1] ^= 1;
    ++loads;
}
BOOT_U64 x86_exception_environment(void) { return environment; }
int x86_irq_cpu_state(void) { return 1; }
int x86_irq_return_test(void) { return 0; }
int x86_irq_spurious_test(void) { return 0; }
__declspec(noreturn) void x86_kernel_stop(BOOT_U64 marker)
{
    (void)marker;
    for (;;) { }
}

// ====== gate eccezioni identici al profilo gia installato ===== //

static void reset(void)
{
    BOOT_U32 vector, byte;
    for (vector = 0; vector < 32; ++vector) {
        for (byte = 0; byte < 16; ++byte) exceptions.Gates[vector][byte] = 0;
        exceptions.Gates[vector][2] = 8;
        exceptions.Gates[vector][4] = vector == 2 ? 2 : vector == 8 ? 3 : 1;
        exceptions.Gates[vector][5] = 0x8e;
        exceptions.Gates[vector][8] = 1;
    }
    for (vector = 0; vector < 224; ++vector) targets[vector] = T + vector * 8;
    active.Bytes[0] = 255; active.Bytes[1] = 1;
    for (byte = 0; byte < 8; ++byte) active.Bytes[byte+2] = (unsigned char)((BOOT_U64)&exceptions >> (byte*8));
    environment = 1; loads = fault = 0;
}

// ====== ogni byte corrotto della nuova IDT viene riconosciuto ===== //

__declspec(dllexport) int run_irq_table_tests(void)
{
    BOOT_U32 index;
    reset();
    if (!x86_irq_prepare(&table,&pointer,&exceptions,targets,T,T+4096) ||
        !x86_irq_verify(&table,&pointer,&exceptions,targets,T,T+4096)) return 1;
    for (index = 0; index < sizeof(table); ++index) {
        ((unsigned char *)&table)[index] ^= 1;
        if (x86_irq_verify(&table,&pointer,&exceptions,targets,T,T+4096)) return 2;
        ((unsigned char *)&table)[index] ^= 1;
    }
    if (!x86_irq_activate(&pointer) || loads != 1) return 3;
    reset(); fault = 1;
    if (x86_irq_activate(&pointer) || loads != 2 || active.Bytes[1] != 1) return 4;
    reset(); fault = 2;
    if (x86_irq_activate(&pointer) != -1 || loads != 2) return 5;
    reset(); environment = 0;
    if (x86_irq_activate(&pointer) || loads) return 6;
    reset(); targets[223] = T + 4096;
    if (x86_irq_prepare(&table,&pointer,&exceptions,targets,T,T+4096)) return 7;
    reset(); exceptions.Gates[2][4] = 1;
    if (x86_irq_prepare(&table,&pointer,&exceptions,targets,T,T+4096)) return 8;
    reset();
    if (x86_irq_prepare(&table,(X86_KERNEL_IDTR *)&table,&exceptions,targets,T,T+4096)) return 9;
    return 0;
}
