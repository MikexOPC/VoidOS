#include "prepare.h"
#include "console/log.h"

// ====== bozza sullo stack corrente senza LGDT o modifica dei selettori ====== //

EFI_STATUS boot_prepare_gdt_draft(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *console, BOOT_GDT_DRAFT *draft)
{
    EFI_STATUS status;
    if (!draft)
        return EFI_INVALID_PARAMETER;
    status = boot_section(console, L"descrittori segmenti x64");
    if (status != EFI_SUCCESS)
        return status;
    if (!x86_prepare_boot_gdt(&draft->Table, &draft->Pointer, (UINT64)(UINTN)&draft->Table) ||
        !x86_validate_boot_gdt(&draft->Table, &draft->Pointer, (UINT64)(UINTN)&draft->Table)) {
        boot_log(console, BOOT_FAIL, L"preparazione descrittori x64 fallita.");
        return EFI_DEVICE_ERROR;
    }
    return boot_log(console, BOOT_OK, L"GDT ring 0 e operando GDTR verificati; descrittori ancora inattivi.");
}
