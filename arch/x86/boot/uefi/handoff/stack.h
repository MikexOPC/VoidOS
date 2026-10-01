#ifndef OS_BOOT_STACK_H
#define OS_BOOT_STACK_H
#include "file_protocol.h"
#include "arch/x86/configs/boot_profile.h"

#define BOOT_STACK_PAGES X86_BOOT_STACK_PAGES

// ====== proprietario dello stack iniziale di 64 KiB ====== //

typedef struct {
    UINT64 PhysicalBase;
    UINTN Pages;
    UINT64 Top;
} BOOT_KERNEL_STACK;

EFI_STATUS boot_allocate_stack(EFI_BOOT_SERVICES_PREFIX *services, BOOT_KERNEL_STACK *stack);
EFI_STATUS boot_release_stack(EFI_BOOT_SERVICES_PREFIX *services, BOOT_KERNEL_STACK *stack);
#endif
