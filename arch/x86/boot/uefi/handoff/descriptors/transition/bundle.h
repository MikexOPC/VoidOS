#ifndef OS_BOOT_DESCRIPTOR_BUNDLE_H
#define OS_BOOT_DESCRIPTOR_BUNDLE_H

#include "arch/x86/lib/descriptors/gdt.h"
#include "arch/x86/lib/descriptors/idt.h"
#include "arch/x86/lib/descriptors/tss.h"

#define BOOT_DESCRIPTOR_SIGNATURE UINT64_C(0x3144534544544f42)
#define BOOT_TSS_SELECTOR 24U
#define BOOT_DESCRIPTOR_OFFSET(object, member) \
    ((uint32_t)((const uint8_t *)&(object)->member - (const uint8_t *)(object)))

// ====== descrittori e indirizzi preparati prima della transizione ====== //

typedef struct {
    uint64_t LinearBase;
    uint64_t PrivilegeStack;
    uint64_t ExceptionStack;
    uint64_t Targets[X86_BOOT_EXCEPTION_COUNT];
} BOOT_DESCRIPTOR_LAYOUT;

typedef struct {
    uint64_t Signature;
    X86_BOOT_GDT Gdt;
    X86_TSS_DESCRIPTOR TaskDescriptor;
    X86_GDT_POINTER Gdtr;
    X86_IDT_POINTER Idtr;
    uint8_t ReservedBeforeTask[12];
#ifdef __cplusplus
    alignas(16) X86_BOOT_TSS Task;
#else
    _Alignas(16) X86_BOOT_TSS Task;
#endif
    uint8_t ReservedBeforeIdt[8];
    X86_BOOT_IDT Idt;
} BOOT_DESCRIPTOR_BUNDLE;

#ifdef __cplusplus

static_assert(sizeof(BOOT_DESCRIPTOR_BUNDLE) == 704, "Layout descrittori entro una pagina");
extern "C" {
#else
_Static_assert(sizeof(BOOT_DESCRIPTOR_BUNDLE) == 704, "Layout descrittori entro una pagina");
#endif

int boot_prepare_descriptor_bundle(BOOT_DESCRIPTOR_BUNDLE *bundle, const BOOT_DESCRIPTOR_LAYOUT *layout);
int boot_validate_descriptor_bundle(const BOOT_DESCRIPTOR_BUNDLE *bundle, const BOOT_DESCRIPTOR_LAYOUT *layout);
void boot_invalidate_descriptor_bundle(BOOT_DESCRIPTOR_BUNDLE *bundle);

#ifdef __cplusplus
}
#endif
#endif
