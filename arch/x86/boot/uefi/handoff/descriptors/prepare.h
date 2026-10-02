#ifndef OS_BOOT_PREPARE_GDT_H
#define OS_BOOT_PREPARE_GDT_H
#include "uefi.h"
#include "arch/x86/lib/descriptors/gdt.h"

typedef struct {
    X86_BOOT_GDT Table;
    X86_GDT_POINTER Pointer;
} BOOT_GDT_DRAFT;

EFI_STATUS boot_prepare_gdt_draft(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *console, BOOT_GDT_DRAFT *draft);
#endif
