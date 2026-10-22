#include "arch/x86/kernel/drivers/memory/iommu/intel/invalidation/qi.h"
#include <stdio.h>
#include <string.h>
__declspec(align(4096)) static unsigned char page[4096];
static unsigned char *current;

// ====== x86_window_bytes ======

volatile unsigned char *x86_window_bytes(void) { return current; }

// ====== main ======

int main(void)
{
    BOOT_U32 line=x86_qi_cpu_line();
    if (!line) {puts("SKIP native CPU cache instructions: CPUID unsupported");return 0;}
    memset(page,0xa5,sizeof(page));
    if (x86_qi_clean_page()) return 1;
    current=page+1; if (x86_qi_clean_page()) return 1;
    current=page; if (!x86_qi_clean_page()) return 1;
    for (unsigned index=0; index<4096; ++index) if (page[index]!=0xa5) return 1;
    printf("PASS native CPUID/CLFLUSH/MFENCE, line=%u bytes; null/alignment/content checked, no physical IOMMU visibility claim\n",line);
    return 0;
}
