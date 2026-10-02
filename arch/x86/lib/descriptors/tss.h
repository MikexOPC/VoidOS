#ifndef OS_X86_BOOT_TSS_H
#define OS_X86_BOOT_TSS_H
#include <stdint.h>

#define X86_BOOT_EXCEPTION_STACK_BYTES UINT64_C(16384)

// ====== formato TSS x64 e descrittore sistema a sedici byte ====== //

typedef struct { uint8_t Bytes[104]; } X86_BOOT_TSS;
typedef struct { uint8_t Bytes[16]; } X86_TSS_DESCRIPTOR;

#ifdef __cplusplus
static_assert(sizeof(X86_BOOT_TSS) == 104, "TSS x64");
extern "C" {
#else
_Static_assert(sizeof(X86_BOOT_TSS) == 104, "TSS x64");
#endif

int x86_prepare_boot_tss(X86_BOOT_TSS *tss, X86_TSS_DESCRIPTOR *descriptor,
                         uint64_t tss_base, uint64_t privilege_stack, uint64_t exception_stack);
int x86_validate_boot_tss(const X86_BOOT_TSS *tss, const X86_TSS_DESCRIPTOR *descriptor,
                          uint64_t tss_base, uint64_t privilege_stack, uint64_t exception_stack);

#ifdef __cplusplus
}
#endif
#endif
