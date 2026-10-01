#ifndef OS_X86_LIB_PROCESSOR_ACCESS_H
#define OS_X86_LIB_PROCESSOR_ACCESS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ====== valori grezzi dei registri senza dipendenze dal firmware ====== //

typedef struct {
    uint64_t Cr0;
    uint64_t Cr3;
    uint64_t Cr4;
    uint64_t Efer;
} X86_PAGING_REGISTERS;

void x86_query_cpuid(uint32_t leaf, uint32_t registers[4]);
int x86_read_paging_registers(X86_PAGING_REGISTERS *result);

#ifdef __cplusplus
}
#endif

#endif
