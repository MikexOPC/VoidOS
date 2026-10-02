#include "arch/x86/kernel/exceptions/terminal.h"
const unsigned char x86_kernel_fault_begin[1] = {0}, x86_kernel_fault_end[1] = {0};
const BOOT_U64 x86_kernel_fault_targets[32] = {0};
static X86_KERNEL_IDT table;
static X86_KERNEL_IDTR pointer;
static BOOT_U64 targets[32];

// ====== mock senza istruzioni privilegiate nel processo host ===== //

void x86_exception_read_idt(X86_KERNEL_IDTR *output) { (void)output; }

// ====== nessun caricamento di IDT nei test host ===== //

void x86_exception_load_idt(const X86_KERNEL_IDTR *input) { (void)input; }

// ====== ambiente host rifiutato prima di operazioni privilegiate ===== //

BOOT_U64 x86_exception_environment(void) { return 0; }

// ====== confronto indipendente dei byte significativi e dei riservati ===== //

static int expected_gate(BOOT_U32 vector)
{
    const unsigned char *gate = table.Gates[vector];
    BOOT_U64 address = (BOOT_U64)gate[0] | ((BOOT_U64)gate[1] << 8) |
        ((BOOT_U64)gate[6] << 16) | ((BOOT_U64)gate[7] << 24);
    BOOT_U32 byte;
    for (byte = 8; byte < 12; ++byte) address |= (BOOT_U64)gate[byte] << ((byte - 4) * 8);
    if (address != targets[vector] || gate[2] != 8 || gate[3] || gate[4] != 1 || gate[5] != 0x8e) return 0;
    for (byte = 12; byte < 16; ++byte) if (gate[byte]) return 0;
    return 1;
}

// ====== ogni byte del gate e dell'operando deve essere verificato ===== //

__declspec(dllexport) int run_exception_tests(void)
{
    BOOT_U32 vector, byte;
    const BOOT_U64 begin = 0x100000000ULL, end = begin + 4096;
    for (vector = 0; vector < 32; ++vector) targets[vector] = begin + vector * 16;
    if (!x86_exception_prepare(&table, &pointer, targets, begin, end)) return 1;
    for (vector = 0; vector < 32; ++vector) {
        if (!expected_gate(vector)) return 2;
        for (byte = 0; byte < 16; ++byte) {
            table.Gates[vector][byte] ^= 1;
            if (x86_exception_verify(&table, &pointer, targets, begin, end)) return 3;
            table.Gates[vector][byte] ^= 1;
        }
    }
    for (byte = 0; byte < 10; ++byte) {
        pointer.Bytes[byte] ^= 1;
        if (x86_exception_verify(&table, &pointer, targets, begin, end)) return 4;
        pointer.Bytes[byte] ^= 1;
    }
    targets[31] = end;
    if (x86_exception_prepare(&table, &pointer, targets, begin, end)) return 5;
    targets[31] = begin + 31 * 16;
    if (!x86_exception_verify(&table, &pointer, targets, begin, end)) return 6;
    if (x86_exception_prepare(0, &pointer, targets, begin, end) ||
        x86_exception_prepare(&table, 0, targets, begin, end) ||
        x86_exception_prepare(&table, &pointer, 0, begin, end) ||
        x86_exception_prepare(&table, (X86_KERNEL_IDTR *)&table, targets, begin, end) ||
        x86_exception_prepare(&table, &pointer, (const BOOT_U64 *)&table, begin, end) ||
        x86_exception_prepare((X86_KERNEL_IDT *)((unsigned char *)&table + 1), &pointer, targets, begin, end)) return 7;
    if (x86_exception_initialize()) return 8;
    return 0;
}
