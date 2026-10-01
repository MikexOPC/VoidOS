#include "console/log.h"

static EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL console;
static EFI_SIMPLE_TEXT_OUTPUT_MODE mode;
static UINTN attributes[4];
static UINTN attribute_calls, output_calls, used;
static UINTN fail_output;
static EFI_STATUS color_status;
static int fail_restore;
static UINTN current_attribute, output_attributes[3];
static CHAR16 captured[128];

// ====== simulazione attributi e scrittura ====== //

static EFI_STATUS EFIAPI set_attribute(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *self, UINTN value)
{
    if (self != &console || attribute_calls >= 4)
        return EFI_INVALID_PARAMETER;
    attributes[attribute_calls++] = value;
    if (fail_restore && attribute_calls == 2)
        return EFI_DEVICE_ERROR;
    if (color_status == EFI_SUCCESS)
        current_attribute = value;
    return color_status;
}

static EFI_STATUS EFIAPI output(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *self, CHAR16 *text)
{
    UINTN i;
    if (self != &console || !text)
        return EFI_INVALID_PARAMETER;
    ++output_calls;
    if (output_calls <= 3)
        output_attributes[output_calls - 1] = current_attribute;
    if (output_calls == fail_output)
        return EFI_DEVICE_ERROR;
    for (i = 0; text[i]; ++i) {
        if (used >= 127)
            return EFI_DEVICE_ERROR;
        captured[used++] = text[i];
    }
    captured[used] = 0;
    return EFI_SUCCESS;
}

static int matches(CHAR16 *expected)
{
    UINTN i;
    for (i = 0; expected[i]; ++i)
        if (captured[i] != expected[i])
            return 0;
    return captured[i] == 0;
}

// ====== preparazione console ====== //

static void prepare(void)
{
    mode.Attribute = 0x17;
    console.Mode = &mode;
    console.OutputString = output;
    console.SetAttribute = set_attribute;
    attribute_calls = output_calls = used = fail_output = 0;
    color_status = EFI_SUCCESS;
    fail_restore = 0;
    current_attribute = 0x17;
    captured[0] = 0;
}

// ====== livelli colori fallback ed errori ====== //

__declspec(dllexport) int run_log_tests(void)
{
    static CHAR16 *expected[] = {
        L"[  OK  ] prova\r\n\r\n", L"[ INFO ] prova\r\n\r\n",
        L"[ WARN ] prova\r\n\r\n", L"[ FAIL ] prova\r\n\r\n"
    };
    static UINTN colors[] = {10, 9, 14, 12};
    UINTN level, failure;
    for (level = 0; level < 4; ++level) {
        prepare();
        if (boot_log(&console, (BOOT_LOG_LEVEL)level, L"prova") != EFI_SUCCESS ||
            !matches(expected[level]) || attribute_calls != 2 || output_calls != 3 ||
            attributes[0] != colors[level] || attributes[1] != 0x17 ||
            output_attributes[0] != colors[level] || output_attributes[1] != 0x17 ||
            output_attributes[2] != 0x17)
            return 301 + (int)level;
    }
    prepare();
    if (boot_section(&console, L"prova") != EFI_SUCCESS ||
        !matches(L"# ============ prova ============ #\r\n\r\n") || attribute_calls ||
        output_attributes[0] != 0x17 || output_attributes[1] != 0x17 || output_attributes[2] != 0x17)
        return 305;
    for (failure = 1; failure <= 3; ++failure) {
        prepare();
        fail_output = failure;
        if (boot_log(&console, BOOT_OK, L"prova") != EFI_DEVICE_ERROR ||
            output_calls != failure || attribute_calls != 2 || attributes[1] != 0x17)
            return 306;
    }
    prepare();
    color_status = EFI_UNSUPPORTED;
    if (boot_log(&console, BOOT_WARN, L"prova") != EFI_SUCCESS || !matches(expected[2]))
        return 307;
    prepare();
    console.SetAttribute = 0;
    if (boot_log(&console, BOOT_INFO, L"prova") != EFI_SUCCESS ||
        attribute_calls != 0 || !matches(expected[1]))
        return 308;
    prepare();
    console.Mode = 0;
    if (boot_log(&console, BOOT_OK, L"prova") != EFI_SUCCESS || attributes[1] != 7)
        return 309;
    prepare();
    if (boot_log(&console, (BOOT_LOG_LEVEL)-1, L"prova") != EFI_INVALID_PARAMETER ||
        boot_log(&console, BOOT_OK, 0) != EFI_INVALID_PARAMETER ||
        boot_section(0, L"prova") != EFI_INVALID_PARAMETER || output_calls != 0)
        return 310;
    prepare();
    fail_restore = 1;
    if (boot_log(&console, BOOT_OK, L"prova") != EFI_DEVICE_ERROR || output_calls != 1 ||
        !matches(L"[  OK  ] "))
        return 311;
    prepare();
    fail_restore = 1;
    fail_output = 1;
    if (boot_log(&console, BOOT_FAIL, L"prova") != EFI_DEVICE_ERROR || attribute_calls != 2)
        return 312;
    return 0;
}
