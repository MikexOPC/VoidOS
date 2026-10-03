#include "arch/x86/kernel/exceptions/terminal.h"
const unsigned char x86_kernel_fault_begin[1] = {0}, x86_kernel_fault_end[1] = {0};
const BOOT_U64 x86_kernel_fault_targets[32] = {0};
static X86_KERNEL_IDT table;
static X86_KERNEL_IDTR pointer;
static BOOT_U64 targets[32];
static X86_KERNEL_IDTR active;
static BOOT_U32 environment, load_count, load_fault;

// ====== mock senza istruzioni privilegiate nel processo host ===== //

void x86_exception_read_idt(X86_KERNEL_IDTR *output)
{
    BOOT_U32 byte;
    for (byte = 0; byte < 10; ++byte) output->Bytes[byte] = active.Bytes[byte];
}

// ====== nessun caricamento di IDT nei test host ===== //

void x86_exception_load_idt(const X86_KERNEL_IDTR *input)
{
    BOOT_U32 byte;
    ++load_count;
    for (byte = 0; byte < 10; ++byte) active.Bytes[byte] = input->Bytes[byte];
    if (load_fault == 2 || (load_fault == 1 && load_count == 1)) active.Bytes[1] ^= 1;
}

// ====== ambiente host rifiutato prima di operazioni privilegiate ===== //

BOOT_U64 x86_exception_environment(void) { return environment; }

// ====== operando mock con intervallo controllato prima della prova ===== //

static void reset_idtr(BOOT_U64 base)
{
    BOOT_U32 byte;
    environment = 1;
    load_count = 0;
    load_fault = 0;
    active.Bytes[0] = 255;
    active.Bytes[1] = 1;
    for (byte = 0; byte < 8; ++byte) active.Bytes[byte + 2] = (unsigned char)(base >> (byte * 8));
}

// ====== attivazione readback e rollback verificati senza LIDT reale ===== //

static int activation_paths(void)
{
    BOOT_U32 byte;
    const BOOT_U64 old_base = 0x200000000ULL;
    reset_idtr(old_base);
    environment = 0;
    if (x86_exception_activate(&pointer) || load_count) return 11;
    reset_idtr(X86_BOOT_LOW_ADDRESS_LIMIT - 16);
    if (x86_exception_activate(&pointer) || load_count) return 12;
    reset_idtr(old_base);
    active.Bytes[1] = 2;
    if (x86_exception_activate(&pointer) || load_count) return 13;
    reset_idtr(old_base);
    pointer.Bytes[0] ^= 1;
    if (x86_exception_activate(&pointer) || load_count) return 14;
    pointer.Bytes[0] ^= 1;
    if (x86_exception_activate(0) || load_count) return 15;
    if (x86_exception_activate((const X86_KERNEL_IDTR *)~0ULL) || load_count) return 16;
    if (x86_exception_activate(&pointer) != 1 || load_count != 1) return 17;
    for (byte = 0; byte < 10; ++byte) if (active.Bytes[byte] != pointer.Bytes[byte]) return 18;
    reset_idtr(old_base);
    load_fault = 1;
    if (x86_exception_activate(&pointer) || load_count != 2) return 19;
    if (active.Bytes[0] != 255 || active.Bytes[1] != 1) return 20;
    for (byte = 0; byte < 8; ++byte) {
        if (active.Bytes[byte + 2] != (unsigned char)(old_base >> (byte * 8))) return 21;
    }
    reset_idtr(old_base);
    load_fault = 2;
    if (x86_exception_activate(&pointer) != -1 || load_count != 2) return 22;
    environment = 0;
    return 0;
}

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

// ====== indirizzi diversi su tutti i byte dell'offset a sessantaquattro bit ===== //

static int generated_targets(void)
{
    BOOT_U64 state = 0x13572468abcdefULL;
    BOOT_U64 begin, end, base;
    BOOT_U32 round, vector, byte;
    for (round = 0; round < 256; ++round) {
        state = state * 6364136223846793005ULL + 1;
        begin = 0x100000000ULL + (state & 0x1ffffffff000ULL);
        end = begin + 4096;
        for (vector = 0; vector < 32; ++vector) {
            state = state * 6364136223846793005ULL + 1;
            targets[vector] = begin + (state >> 32) % 4096;
        }
        if (!x86_exception_prepare(&table, &pointer, targets, 32, begin, end)) {
            return 23;
        }
        for (vector = 0; vector < 32; ++vector) {
            if (!expected_gate(vector)) {
                return 24;
            }
        }
        base = 0;
        for (byte = 0; byte < 8; ++byte) {
            base |= (BOOT_U64)pointer.Bytes[byte + 2] << (byte * 8);
        }
        if (base != (BOOT_U64)&table || pointer.Bytes[0] != 255 || pointer.Bytes[1] != 1) {
            return 25;
        }
        targets[17] = end;
        if (x86_exception_prepare(&table, &pointer, targets, 32, begin, end)) {
            return 26;
        }
        // Un errore tra gli ingressi non deve pubblicare gate parziali.
        for (vector = 0; vector < 32; ++vector) {
            if (vector != 17 && !expected_gate(vector)) {
                return 27;
            }
        }
    }
    return 0;
}

// ====== ogni byte del gate e dell'operando deve essere verificato ===== //

static int packed_operand(void)
{
    unsigned char storage[11];
    X86_KERNEL_IDTR *unaligned = (X86_KERNEL_IDTR *)(storage + 1);
    BOOT_U32 byte;
    if (!x86_exception_prepare(&table, unaligned, targets, 32, 0x100000000ULL, 0x100001000ULL)) return 28;
    for (byte = 0; byte < 10; ++byte) {
        if (unaligned->Bytes[byte] != pointer.Bytes[byte]) return 29;
    }
    return 0;
}

// ====== regressione completa dei gate e dei percorsi privilegiati mock ===== //

__declspec(dllexport) int run_exception_tests(void)
{
    BOOT_U32 vector, byte, bit;
    int result;
    const BOOT_U64 begin = 0x100000000ULL, end = begin + 4096;
    for (vector = 0; vector < 32; ++vector) targets[vector] = begin + vector * 16;
    if (!x86_exception_prepare(&table, &pointer, targets, 32, begin, end)) return 1;
    for (vector = 0; vector < 32; ++vector) {
        if (!expected_gate(vector)) return 2;
        for (byte = 0; byte < 16; ++byte) {
            for (bit = 0; bit < 8; ++bit) {
                table.Gates[vector][byte] ^= (unsigned char)(1U << bit);
                if (x86_exception_verify(&table, &pointer, targets, 32, begin, end)) return 3;
                table.Gates[vector][byte] ^= (unsigned char)(1U << bit);
            }
        }
    }
    for (byte = 0; byte < 10; ++byte) {
        for (bit = 0; bit < 8; ++bit) {
            pointer.Bytes[byte] ^= (unsigned char)(1U << bit);
            if (x86_exception_verify(&table, &pointer, targets, 32, begin, end)) return 4;
            pointer.Bytes[byte] ^= (unsigned char)(1U << bit);
        }
    }
    targets[31] = end;
    if (x86_exception_prepare(&table, &pointer, targets, 32, begin, end)) return 5;
    targets[31] = begin + 31 * 16;
    if (!x86_exception_verify(&table, &pointer, targets, 32, begin, end)) return 6;
    if (x86_exception_prepare(0, &pointer, targets, 32, begin, end) ||
        x86_exception_prepare(&table, 0, targets, 32, begin, end) ||
        x86_exception_prepare(&table, &pointer, 0, 32, begin, end) ||
        x86_exception_prepare(&table, (X86_KERNEL_IDTR *)&table, targets, 32, begin, end) ||
        x86_exception_prepare(&table, &pointer, (const BOOT_U64 *)&table, 32, begin, end) ||
        x86_exception_prepare((X86_KERNEL_IDT *)((unsigned char *)&table + 1), &pointer, targets, 32, begin, end)) return 7;
    if (x86_exception_prepare(&table, &pointer, targets, 31, begin, end) ||
        x86_exception_prepare(&table, &pointer, targets, 33, begin, end) ||
        x86_exception_prepare(&table, &pointer, (const BOOT_U64 *)8, 0, begin, end) ||
        x86_exception_prepare(&table, (X86_KERNEL_IDTR *)(X86_BOOT_LOW_ADDRESS_LIMIT - 8), targets, 32, begin, end) ||
        x86_exception_prepare(&table, &pointer, (const BOOT_U64 *)(X86_BOOT_LOW_ADDRESS_LIMIT - 8), 32, begin, end) ||
        x86_exception_prepare(&table, (X86_KERNEL_IDTR *)~0ULL, targets, 32, begin, end)) return 9;
    if (!x86_exception_verify(&table, &pointer, targets, 32, begin, end)) return 10;
    if (x86_exception_initialize()) return 8;
    result = activation_paths();
    if (result) return result;
    result = packed_operand();
    if (result) return result;
    result = generated_targets();
    if (result) return result;
    return 0;
}
