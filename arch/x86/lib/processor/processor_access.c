#include "arch/x86/lib/processor/processor_access.h"

#if !defined(_M_X64)
#error This processor access implementation requires the Microsoft x64 ABI.
#endif

void __cpuid(int registers[4], int leaf);
#pragma intrinsic(__cpuid)
unsigned __int64 __readcr0(void);
unsigned __int64 __readcr3(void);
unsigned __int64 __readcr4(void);
unsigned __int64 __readmsr(unsigned long register_number);
#pragma intrinsic(__readcr0, __readcr3, __readcr4, __readmsr)

_Static_assert(sizeof(X86_PAGING_REGISTERS) == 32, "Unexpected register snapshot ABI");

// ====== istruzione CPUID con subleaf zero ====== //

void x86_query_cpuid(uint32_t leaf, uint32_t registers[4])
{
    int values[4];
    uint32_t index;
    if (!registers)
        return;
    __cpuid(values, (int)leaf);
    for (index = 0; index < 4; ++index)
        registers[index] = (uint32_t)values[index];
}

// ====== lettura privilegiata con pubblicazione dopo il ricontrollo CR3 ====== //

int x86_read_paging_registers(X86_PAGING_REGISTERS *result)
{
    X86_PAGING_REGISTERS registers;
    if (!result)
        return 0;
    registers.Cr3 = __readcr3();
    registers.Cr0 = __readcr0();
    registers.Cr4 = __readcr4();
    registers.Efer = __readmsr(0xc0000080UL);
    if (__readcr3() != registers.Cr3)
        return 0;
    result->Cr0 = registers.Cr0;
    result->Cr3 = registers.Cr3;
    result->Cr4 = registers.Cr4;
    result->Efer = registers.Efer;
    return 1;
}
