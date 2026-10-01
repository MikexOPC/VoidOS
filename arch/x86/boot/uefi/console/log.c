#include "log.h"
#define KEEP_CONSOLE_ATTRIBUTE 128

static CHAR16 *tags[] = {L"[  OK  ] ", L"[ INFO ] ", L"[ WARN ] ", L"[ FAIL ] "};
static const UINTN colors[] = {10, 9, 14, 12};

// ====== scrittura con ripristino attributo ====== //

static EFI_STATUS write_parts(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *console, UINTN color, CHAR16 *first, CHAR16 *second, CHAR16 *third)
{
    UINTN original;
    EFI_STATUS status;
    EFI_STATUS color_status = EFI_UNSUPPORTED;
    EFI_STATUS restore_status;
    if (!console || !console->OutputString || !first || !second || !third)
        return EFI_INVALID_PARAMETER;
    original = console->Mode ? (UINTN)console->Mode->Attribute & 127 : 7;
    if (console->SetAttribute && color != KEEP_CONSOLE_ATTRIBUTE)
        color_status = console->SetAttribute(console, color);
    status = console->OutputString(console, first);
    if (console->SetAttribute && color != KEEP_CONSOLE_ATTRIBUTE) {
        restore_status = console->SetAttribute(console, original);
        if (status == EFI_SUCCESS && color_status == EFI_SUCCESS && restore_status != EFI_SUCCESS)
            return restore_status;
    }
    if (status == EFI_SUCCESS)
        status = console->OutputString(console, second);
    if (status == EFI_SUCCESS)
        status = console->OutputString(console, third);
    return status;
}

// ====== riga con livello ====== //

EFI_STATUS boot_log(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *console, BOOT_LOG_LEVEL level, CHAR16 *message)
{
    if ((unsigned int)level > BOOT_FAIL)
        return EFI_INVALID_PARAMETER;
    return write_parts(console, colors[level], tags[level], message, L"\r\n\r\n");
}

// ====== separatore su riga distinta ====== //

EFI_STATUS boot_section(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *console, CHAR16 *name)
{
    return write_parts(console, KEEP_CONSOLE_ATTRIBUTE,
        L"# ============ ", name, L" ============ #\r\n\r\n");
}

// ====== valore esadecimale con etichetta limitata e buffer sullo stack ====== //

EFI_STATUS boot_log_hex(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *console, CHAR16 *label, UINT64 value)
{
    CHAR16 message[64];
    UINTN length = 0, digit;
    static const CHAR16 digits[] = L"0123456789ABCDEF";
    if (!label)
        return EFI_INVALID_PARAMETER;
    while (length < 40 && label[length]) {
        message[length] = label[length];
        ++length;
    }
    if (length == 40 && label[length])
        return EFI_INVALID_PARAMETER;
    message[length++] = '0';
    message[length++] = 'x';
    for (digit = 0; digit < 16; ++digit)
        message[length++] = digits[(value >> ((15 - digit) * 4)) & 15];
    message[length] = 0;
    return boot_log(console, BOOT_INFO, message);
}
