#include "uefi.h"
#include "console/log.h"
#include "handoff/descriptors/prepare.h"
#include "handoff/descriptors/exceptions.h"
#include "arch/x86/lib/descriptors/tss.h"
#include "handoff/descriptors/transition/bundle.h"
#include "handoff/transition/capsule.h"
#include "memory/map.h"

#ifndef BOOT_TEST_EXCEPTION_VECTOR
#error Select the exception vector explicitly
#endif

__declspec(noreturn) void boot_test_trigger_exception(const X86_GDT_POINTER *gdt,
                                                      const X86_IDT_POINTER *idt, UINT64 vector);
__declspec(noreturn) void boot_test_trigger_capsule(const X86_GDT_POINTER *gdt,
    const X86_IDT_POINTER *idt, UINT64 code, UINT64 stack, UINT64 entry, UINT64 contract);
extern const UINT8 boot_test_capsule_target[], boot_test_capsule_stop[];

static __declspec(align(4096)) UINT8 privilege_stack[X86_BOOT_EXCEPTION_STACK_BYTES];
static __declspec(align(4096)) UINT8 exception_stack[X86_BOOT_EXCEPTION_STACK_BYTES];
static __declspec(align(4096)) BOOT_DESCRIPTOR_BUNDLE descriptors;
#if BOOT_TEST_EXCEPTION_VECTOR == 32
static __declspec(align(4096)) UINT8 kernel_stack[65536];
#endif

// ====== immagine di test separata che non ritorna al firmware ====== //

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *system_table)
{
    BOOT_DESCRIPTOR_LAYOUT layout;
    EFI_STATUS status;
    UINT32 vector;
    UINT64 code = 0;
    EFI_BOOT_SERVICES_PREFIX *services;
    (void)image;
    if (!system_table || !system_table->ConOut) return EFI_INVALID_PARAMETER;
    services = (EFI_BOOT_SERVICES_PREFIX *)system_table->BootServices;
    if (!services || !services->AllocatePages || !services->FreePages) return EFI_UNSUPPORTED;
    status = services->AllocatePages(0, 1, 1, &code);
    if (status != EFI_SUCCESS) return status;
    if (code < 4096 || (code & 4095) || code > X86_BOOT_LOW_ADDRESS_LIMIT - 4096)
        return EFI_DEVICE_ERROR;
    if (!boot_prepare_capsule((UINT8 *)(UINTN)code, 4096)) {
        status = EFI_DEVICE_ERROR;
        goto cleanup;
    }
    layout.LinearBase = (UINT64)(UINTN)&descriptors;
    layout.PrivilegeStack = (UINT64)(UINTN)privilege_stack;
    layout.ExceptionStack = (UINT64)(UINTN)exception_stack;
    for (vector = 0; vector < X86_BOOT_EXCEPTION_COUNT; ++vector)
        layout.Targets[vector] = code + BOOT_CAPSULE_VECTOR_OFFSET + vector * BOOT_CAPSULE_VECTOR_STRIDE;
    if (!boot_prepare_descriptor_bundle(&descriptors, &layout) ||
        !boot_validate_descriptor_bundle(&descriptors, &layout)) {
        status = EFI_DEVICE_ERROR;
        goto cleanup;
    }
    status = boot_log_hex(system_table->ConOut, L"EXCEPTION_STACK: ", (UINT64)(UINTN)exception_stack);
    if (status != EFI_SUCCESS) goto cleanup;
    status = boot_log_hex(system_table->ConOut, L"TSS_ACCESS: ", (UINT64)(UINTN)&descriptors.TaskDescriptor.Bytes[5]);
    if (status != EFI_SUCCESS) goto cleanup;
    status = boot_log_hex(system_table->ConOut, L"EXCEPTION_STOP: ",
                          code + BOOT_CAPSULE_STOP_OFFSET);
    if (status != EFI_SUCCESS) goto cleanup;
#if BOOT_TEST_EXCEPTION_VECTOR == 32
    if (!boot_capsule_check_addresses(code, (UINT64)(UINTN)boot_test_capsule_target,
        (UINT64)(UINTN)kernel_stack + sizeof(kernel_stack), (UINT64)(UINTN)&descriptors, sizeof(kernel_stack))) {
        status = EFI_DEVICE_ERROR;
        goto cleanup;
    }
    status = boot_log_hex(system_table->ConOut, L"CAPSULE_STACK_TOP: ", (UINT64)(UINTN)kernel_stack + sizeof(kernel_stack));
    if (status == EFI_SUCCESS) status = boot_log_hex(system_table->ConOut, L"CAPSULE_CONTRACT: ", (UINT64)(UINTN)&descriptors);
    if (status == EFI_SUCCESS) status = boot_log_hex(system_table->ConOut, L"CAPSULE_STOP: ", (UINT64)(UINTN)boot_test_capsule_stop);
    if (status != EFI_SUCCESS) goto cleanup;
#endif
    status = boot_log(system_table->ConOut, BOOT_INFO, L"EXCEPTION_PROBE_READY");
    if (status != EFI_SUCCESS) goto cleanup;
    boot_invalidate_descriptor_bundle(&descriptors);
#if BOOT_TEST_EXCEPTION_VECTOR == 32
    boot_test_trigger_capsule(&descriptors.Gdtr, &descriptors.Idtr, code,
        (UINT64)(UINTN)kernel_stack + sizeof(kernel_stack), (UINT64)(UINTN)boot_test_capsule_target,
        (UINT64)(UINTN)&descriptors);
#else
    boot_test_trigger_exception(&descriptors.Gdtr, &descriptors.Idtr, BOOT_TEST_EXCEPTION_VECTOR);
#endif
cleanup:
    services->FreePages(code, 1);
    return status;
}
