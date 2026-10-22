#include "../qi.h"
#include "arch/x86/mm/window/access.h"
#include <intrin.h>
#include <emmintrin.h>

// ====== x86_qi_cpu_line ======

// Intel SDM2A093 CPUID/CLFLUSH3-144: EDX19, EBX[15:8]*8; SDM2B MFENCE4-15 requires EDX26.
BOOT_U32 x86_qi_cpu_line(void)
{
    int registers[4]; __cpuid(registers,0);
    if (registers[0]<1) return 0;
    __cpuid(registers,1);
    return x86_qi_cache_line((BOOT_U32)registers[3],(BOOT_U32)registers[1]);
}

// ====== x86_qi_clean_page ======

int x86_qi_clean_page(void)
{
    const unsigned char *page=(const unsigned char *)x86_window_bytes();
    BOOT_U32 line=x86_qi_cpu_line();
    if (!page || ((BOOT_U64)page&4095) || !line) return 0;
    _ReadWriteBarrier(); _mm_mfence();
    for (BOOT_U32 offset=0; offset<4096; offset+=line) _mm_clflush(page+offset);
    _mm_mfence(); _ReadWriteBarrier();
    return 1;
}
