#include "cpu/paging_state.h"
#include "console/log.h"

// ====== decoder e confronto con registri simulati ====== //

__declspec(dllexport) int run_paging_state_tests(void)
{
    BOOT_PAGING_CAPABILITIES capabilities = {52, 1, 1};
    BOOT_PAGING_STATE before = {0};
    BOOT_PAGING_STATE after;
    before.Cr0 = (1ULL << 31) | 1;
    before.Cr3 = 0x800018;
    before.Cr4 = 1ULL << 5;
    before.Efer = (1ULL << 8) | (1ULL << 10);
    if (boot_decode_paging_state(&capabilities, &before) != EFI_SUCCESS ||
        before.Root != 0x800000 || before.Flags || before.Valid != 1)
        return 1;
    after = before;
    if (boot_compare_paging_state(&before, &after) != EFI_SUCCESS)
        return 2;
    after.Cr3++;
    if (boot_compare_paging_state(&before, &after) != EFI_DEVICE_ERROR)
        return 3;
    after = before;
    after.Flags = BOOT_PAGING_NXE;
    if (boot_compare_paging_state(&before, &after) != EFI_DEVICE_ERROR)
        return 4;
    after = before;
    after.Cr4 |= 1ULL << 12;
    if (boot_decode_paging_state(&capabilities, &after) != EFI_UNSUPPORTED ||
        after.Valid || after.Root || after.Flags || after.Cr3 != before.Cr3)
        return 5;
    if (boot_compare_paging_state(&before, &after) != EFI_INVALID_PARAMETER ||
        boot_decode_paging_state(0, &before) != EFI_INVALID_PARAMETER || before.Valid ||
        boot_decode_paging_state(&capabilities, 0) != EFI_INVALID_PARAMETER)
        return 6;
    return 0;
}

// ====== formato esadecimale e limiti del buffer di diagnostica ====== //

static CHAR16 captured[64];
static UINTN message_count;

static EFI_STATUS EFIAPI capture(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *console, CHAR16 *text)
{
    UINTN index;
    (void)console;
    if (text[0] == '[' || text[0] == '\r')
        return EFI_SUCCESS;
    for (index = 0; index < 64; ++index) {
        captured[index] = text[index];
        if (!text[index]) {
            ++message_count;
            return EFI_SUCCESS;
        }
    }
    return EFI_DEVICE_ERROR;
}

__declspec(dllexport) int run_hex_log_tests(void)
{
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL console = {0};
    CHAR16 label[42];
    UINTN index;
    static const CHAR16 expected[] = L"CR3: 0x0123456789ABCDEF";
    console.OutputString = capture;
    message_count = 0;
    if (boot_log_hex(&console, L"CR3: ", 0x0123456789abcdefULL) != EFI_SUCCESS || message_count != 1)
        return 1;
    for (index = 0; index < sizeof(expected) / sizeof(expected[0]); ++index)
        if (captured[index] != expected[index])
            return 2;
    for (index = 0; index < 41; ++index)
        label[index] = 'x';
    label[41] = 0;
    if (boot_log_hex(&console, label, 0) != EFI_INVALID_PARAMETER || message_count != 1)
        return 3;
    label[40] = 0;
    if (boot_log_hex(&console, label, ~0ULL) != EFI_SUCCESS || captured[58] ||
        boot_log_hex(&console, 0, 0) != EFI_INVALID_PARAMETER)
        return 4;
    return 0;
}
