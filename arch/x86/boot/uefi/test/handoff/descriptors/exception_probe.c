#include "uefi.h"
#include "console/log.h"
#include "handoff/descriptors/prepare.h"
#include "handoff/descriptors/exceptions.h"

#ifndef BOOT_TEST_EXCEPTION_VECTOR
#error Select the exception vector explicitly
#endif

__declspec(noreturn) void boot_test_trigger_exception(const X86_GDT_POINTER *gdt,
                                                      const X86_IDT_POINTER *idt, UINT64 vector);

// ====== immagine di test separata che non ritorna al firmware ====== //

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *system_table)
{
    BOOT_GDT_DRAFT gdt;
    BOOT_IDT_DRAFT idt;
    EFI_STATUS status;
    (void)image;
    if (!system_table || !system_table->ConOut) return EFI_INVALID_PARAMETER;
    if (!x86_prepare_boot_gdt(&gdt.Table, &gdt.Pointer, (UINT64)(UINTN)&gdt.Table) ||
        !x86_prepare_boot_idt(&idt.Table, &idt.Pointer, (UINT64)(UINTN)&idt.Table,
                              x86_boot_exception_targets)) return EFI_DEVICE_ERROR;
    status = boot_log_hex(system_table->ConOut, L"EXCEPTION_STOP: ",
                          (UINT64)(UINTN)x86_boot_exception_stop);
    if (status != EFI_SUCCESS) return status;
    status = boot_log(system_table->ConOut, BOOT_INFO, L"EXCEPTION_PROBE_READY");
    if (status != EFI_SUCCESS) return status;
    boot_test_trigger_exception(&gdt.Pointer, &idt.Pointer, BOOT_TEST_EXCEPTION_VECTOR);
}
