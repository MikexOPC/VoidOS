#include "exceptions.h"
#include "console/log.h"

// ====== ingressi terminali senza LIDT nel percorso firmware ====== //

EFI_STATUS boot_prepare_idt_draft(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *console, BOOT_IDT_DRAFT *draft)
{
    EFI_STATUS status;
    if (!draft) return EFI_INVALID_PARAMETER;
    status = boot_section(console, L"descrittori eccezioni x64");
    if (status != EFI_SUCCESS) return status;
    if (!x86_prepare_boot_idt(&draft->Table, &draft->Pointer, (UINT64)(UINTN)&draft->Table,
                              x86_boot_exception_targets) ||
        !x86_validate_boot_idt(&draft->Table, &draft->Pointer, (UINT64)(UINTN)&draft->Table,
                               x86_boot_exception_targets)) {
        boot_log(console, BOOT_FAIL, L"preparazione descrittori eccezioni fallita.");
        return EFI_DEVICE_ERROR;
    }
    return boot_log(console, BOOT_OK,
                    L"IDT, IDTR e 32 ingressi terminali verificati; eccezioni ancora gestite dal firmware.");
}
