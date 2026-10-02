#include "uefi.h"
#include "console/log.h"
#include "handoff/descriptors/prepare.h"
#include "handoff/descriptors/exceptions.h"
#include "arch/x86/lib/descriptors/tss.h"
#include "handoff/descriptors/transition/bundle.h"

#ifndef BOOT_TEST_EXCEPTION_VECTOR
#error Select the exception vector explicitly
#endif

__declspec(noreturn) void boot_test_trigger_exception(const X86_GDT_POINTER *gdt,
                                                      const X86_IDT_POINTER *idt, UINT64 vector);

static __declspec(align(4096)) UINT8 privilege_stack[X86_BOOT_EXCEPTION_STACK_BYTES];
static __declspec(align(4096)) UINT8 exception_stack[X86_BOOT_EXCEPTION_STACK_BYTES];
static __declspec(align(4096)) BOOT_DESCRIPTOR_BUNDLE descriptors;

// ====== immagine di test separata che non ritorna al firmware ====== //

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *system_table)
{
    BOOT_DESCRIPTOR_LAYOUT layout;
    EFI_STATUS status;
    UINT32 vector;
    (void)image;
    if (!system_table || !system_table->ConOut) return EFI_INVALID_PARAMETER;
    layout.LinearBase = (UINT64)(UINTN)&descriptors;
    layout.PrivilegeStack = (UINT64)(UINTN)privilege_stack;
    layout.ExceptionStack = (UINT64)(UINTN)exception_stack;
    for (vector = 0; vector < X86_BOOT_EXCEPTION_COUNT; ++vector)
        layout.Targets[vector] = x86_boot_exception_targets[vector];
    if (!boot_prepare_descriptor_bundle(&descriptors, &layout) ||
        !boot_validate_descriptor_bundle(&descriptors, &layout)) return EFI_DEVICE_ERROR;
    status = boot_log_hex(system_table->ConOut, L"EXCEPTION_STACK: ", (UINT64)(UINTN)exception_stack);
    if (status != EFI_SUCCESS) return status;
    status = boot_log_hex(system_table->ConOut, L"TSS_ACCESS: ", (UINT64)(UINTN)&descriptors.TaskDescriptor.Bytes[5]);
    if (status != EFI_SUCCESS) return status;
    status = boot_log_hex(system_table->ConOut, L"EXCEPTION_STOP: ",
                          (UINT64)(UINTN)x86_boot_exception_stop);
    if (status != EFI_SUCCESS) return status;
    status = boot_log(system_table->ConOut, BOOT_INFO, L"EXCEPTION_PROBE_READY");
    if (status != EFI_SUCCESS) return status;
    boot_invalidate_descriptor_bundle(&descriptors);
    boot_test_trigger_exception(&descriptors.Gdtr, &descriptors.Idtr, BOOT_TEST_EXCEPTION_VECTOR);
}
