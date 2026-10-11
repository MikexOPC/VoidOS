#include "file_protocol.h"

static UINTN calls;
static EFI_STATUS output_status;
static EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *expected_console;

// ====== console simulata ====== //

static EFI_STATUS EFIAPI mock_output(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *console, CHAR16 *text)
{
    ++calls;
    if (console != expected_console || !text)
        return EFI_INVALID_PARAMETER;
    return output_status;
}

// ====== test ingresso ed errori ====== //

__declspec(dllexport) int run_tests(void)
{
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL console = {0};
    EFI_SYSTEM_TABLE table = {0};
    UINTN console_offset = (UINTN)((UINT8 *)&table.ConOut - (UINT8 *)&table);
    UINTN services_offset = (UINTN)((UINT8 *)&table.BootServices - (UINT8 *)&table);
    UINTN output_offset = (UINTN)((UINT8 *)&console.OutputString - (UINT8 *)&console);
    if (console_offset != 64 || services_offset != 96 || output_offset != 8)
        return 1;
    calls = 0;
    output_status = EFI_SUCCESS;
    expected_console = &console;
    console.OutputString = mock_output;
    table.Hdr.Signature = EFI_SYSTEM_TABLE_SIGNATURE;
    table.Hdr.HeaderSize = sizeof(table);
    table.ConOut = &console;
    if (efi_main(0, &table) != EFI_INVALID_PARAMETER || calls != 15)
        return 2;
    output_status = EFI_DEVICE_ERROR;
    if (efi_main(0, &table) != EFI_DEVICE_ERROR || calls != 16)
        return 3;
    output_status = 1;
    if (efi_main(0, &table) != 1 || calls != 17)
        return 4;
    if (efi_main(0, 0) != EFI_INVALID_PARAMETER)
        return 5;
    table.Hdr.Signature = 0;
    if (efi_main(0, &table) != EFI_INVALID_PARAMETER)
        return 6;
    table.Hdr.Signature = EFI_SYSTEM_TABLE_SIGNATURE;
    table.Hdr.HeaderSize = sizeof(table) - 1;
    if (efi_main(0, &table) != EFI_INVALID_PARAMETER)
        return 7;
    table.Hdr.HeaderSize = sizeof(table);
    table.ConOut = 0;
    if (efi_main(0, &table) != EFI_UNSUPPORTED)
        return 8;
    table.ConOut = &console;
    console.OutputString = 0;
    if (efi_main(0, &table) != EFI_UNSUPPORTED || calls != 17)
        return 9;
    return run_file_tests();
}

