#ifndef OS_BOOT_EXCEPTION_DRAFT_H
#define OS_BOOT_EXCEPTION_DRAFT_H
#include "uefi.h"
#include "arch/x86/lib/descriptors/idt.h"

typedef struct {
    X86_BOOT_IDT Table;
    X86_IDT_POINTER Pointer;
} BOOT_IDT_DRAFT;

extern const uint64_t x86_boot_exception_targets[X86_BOOT_EXCEPTION_COUNT];
extern void x86_boot_exception_stop(void);

EFI_STATUS boot_prepare_idt_draft(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *console, BOOT_IDT_DRAFT *draft);
#endif
