#ifndef OS_X86_BOOT_IDT_H
#define OS_X86_BOOT_IDT_H
#include <stdint.h>

#define X86_BOOT_EXCEPTION_COUNT 32U

// ====== gate IA32e e operando IDTR senza padding ====== //

typedef struct {
#ifdef __cplusplus
    alignas(16) uint8_t Gates[X86_BOOT_EXCEPTION_COUNT][16];
#else
    _Alignas(16) uint8_t Gates[X86_BOOT_EXCEPTION_COUNT][16];
#endif
} X86_BOOT_IDT;

typedef struct {
    uint8_t Bytes[10];
} X86_IDT_POINTER;

#ifdef __cplusplus
static_assert(sizeof(X86_BOOT_IDT) == 512, "IDT eccezioni x64");
static_assert(alignof(X86_BOOT_IDT) == 16, "Allineamento IDT");
static_assert(sizeof(X86_IDT_POINTER) == 10, "Operando IDTR x64");
extern "C" {
#else
_Static_assert(sizeof(X86_BOOT_IDT) == 512, "IDT eccezioni x64");
_Static_assert(_Alignof(X86_BOOT_IDT) == 16, "Allineamento IDT");
_Static_assert(sizeof(X86_IDT_POINTER) == 10, "Operando IDTR x64");
#endif

int x86_prepare_boot_idt(X86_BOOT_IDT *idt, X86_IDT_POINTER *pointer, uint64_t linear_base,
                         const uint64_t targets[X86_BOOT_EXCEPTION_COUNT]);
int x86_validate_boot_idt(const X86_BOOT_IDT *idt, const X86_IDT_POINTER *pointer, uint64_t linear_base,
                          const uint64_t targets[X86_BOOT_EXCEPTION_COUNT]);

#ifdef __cplusplus
}
#endif
#endif
