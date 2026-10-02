#include "uefi.h"
#include "console/log.h"
#include "handoff/descriptors/prepare.h"
#include "handoff/descriptors/exceptions.h"
#include "arch/x86/lib/descriptors/tss.h"

#ifndef BOOT_TEST_EXCEPTION_VECTOR
#error Select the exception vector explicitly
#endif

__declspec(noreturn) void boot_test_trigger_exception(const X86_GDT_POINTER *gdt,
                                                      const X86_IDT_POINTER *idt, UINT64 vector);

static __declspec(align(4096)) UINT8 privilege_stack[X86_BOOT_EXCEPTION_STACK_BYTES];
static __declspec(align(4096)) UINT8 exception_stack[X86_BOOT_EXCEPTION_STACK_BYTES];
static __declspec(align(16)) X86_BOOT_TSS task_state;

// ====== immagine di test separata che non ritorna al firmware ====== //

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *system_table)
{
    struct { X86_BOOT_GDT Table; X86_TSS_DESCRIPTOR Task; } gdt;
    X86_GDT_POINTER gdt_pointer;
    BOOT_IDT_DRAFT idt;
    EFI_STATUS status;
    UINT32 vector;
    (void)image;
    if (!system_table || !system_table->ConOut) return EFI_INVALID_PARAMETER;
    _Static_assert(sizeof(gdt) == 40, "GDT con descrittore TSS contiguo");
    if (!x86_prepare_boot_gdt(&gdt.Table, &gdt_pointer, (UINT64)(UINTN)&gdt.Table) ||
        !x86_prepare_boot_tss(&task_state, &gdt.Task, (UINT64)(UINTN)&task_state,
                              (UINT64)(UINTN)privilege_stack, (UINT64)(UINTN)exception_stack) ||
        !x86_prepare_boot_idt(&idt.Table, &idt.Pointer, (UINT64)(UINTN)&idt.Table,
                              x86_boot_exception_targets)) return EFI_DEVICE_ERROR;
    gdt_pointer.Bytes[0] = 39;
    /* Solo il probe usa IST1 per ogni vettore; il profilo ordinario mantiene IST zero. */
    for (vector = 0; vector < X86_BOOT_EXCEPTION_COUNT; ++vector) idt.Table.Gates[vector][4] = 1;
    status = boot_log_hex(system_table->ConOut, L"EXCEPTION_STACK: ", (UINT64)(UINTN)exception_stack);
    if (status != EFI_SUCCESS) return status;
    status = boot_log_hex(system_table->ConOut, L"TSS_ACCESS: ", (UINT64)(UINTN)&gdt.Task.Bytes[5]);
    if (status != EFI_SUCCESS) return status;
    status = boot_log_hex(system_table->ConOut, L"EXCEPTION_STOP: ",
                          (UINT64)(UINTN)x86_boot_exception_stop);
    if (status != EFI_SUCCESS) return status;
    status = boot_log(system_table->ConOut, BOOT_INFO, L"EXCEPTION_PROBE_READY");
    if (status != EFI_SUCCESS) return status;
    boot_test_trigger_exception(&gdt_pointer, &idt.Pointer, BOOT_TEST_EXCEPTION_VECTOR);
}
