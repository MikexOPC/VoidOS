#ifndef OS_X86_BOOT_GDT_H
#define OS_X86_BOOT_GDT_H
#include <stdint.h>

#define X86_BOOT_CODE_SELECTOR 8U
#define X86_BOOT_DATA_SELECTOR 16U
#define X86_USER_DATA_SELECTOR 0x2bU
#define X86_USER_CODE_SELECTOR 0x33U
// Intel SDM 3A §3.4.5: P=1, DPL=3, S=1, accessed flat RW data and 64-bit RX code.
#define X86_USER_DATA_DESCRIPTOR UINT64_C(0x00cff3000000ffff)
#define X86_USER_CODE_DESCRIPTOR UINT64_C(0x00affb000000ffff)

// ====== tre segmenti e operando GDTR x64 senza padding ====== //

typedef struct {
    uint64_t Entries[3];
} X86_BOOT_GDT;

typedef struct {
    uint8_t Bytes[10];
} X86_GDT_POINTER;

#ifdef __cplusplus
static_assert(sizeof(X86_BOOT_GDT) == 24, "GDT a tre descrittori");
static_assert(sizeof(X86_GDT_POINTER) == 10, "Operando GDTR x64");
extern "C" {
#else
_Static_assert(sizeof(X86_BOOT_GDT) == 24, "GDT a tre descrittori");
_Static_assert(sizeof(X86_GDT_POINTER) == 10, "Operando GDTR x64");
#endif

int x86_prepare_boot_gdt(X86_BOOT_GDT *gdt, X86_GDT_POINTER *pointer, uint64_t linear_base);
int x86_validate_boot_gdt(const X86_BOOT_GDT *gdt, const X86_GDT_POINTER *pointer, uint64_t linear_base);

#ifdef __cplusplus
}
#endif
#endif
