#include "arch/x86/boot/uefi/uefi.h"

int run_hpet_description_tests(void);
int run_hpet_scan_tests(void);

// ====== efi_main ======

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *system_table)
{
    (void)image;
    if (!system_table || !system_table->ConOut ||
        !system_table->ConOut->OutputString) {
        return EFI_INVALID_PARAMETER;
    }
    if (run_hpet_description_tests() != 0 || run_hpet_scan_tests() != 0) {
        system_table->ConOut->OutputString(system_table->ConOut,
            L"[ FAIL ] HPET_DESCRIPTION_TEST_FAILED\r\n");
        return EFI_DEVICE_ERROR;
    }
    return system_table->ConOut->OutputString(system_table->ConOut,
        L"HPET_DESCRIPTION_TEST_READY\r\n");
}
