#include "log.h"
#include "arch/x86/kernel/platform.h"

// ====== testi statici limitati e nessuna dipendenza da printf ====== //

int kernel_text(const char *text)
{
    unsigned int index;
    if (!text) return 0;
    for (index = 0; index < 256; ++index) {
        if (!text[index]) return 1;
        if (text[index] == '\n' && !x86_serial_character('\r')) return 0;
        if (!x86_serial_character(text[index])) return 0;
    }
    return 0;
}

int kernel_section(const char *name)
{
    return kernel_text("\n# ============ ") && kernel_text(name) && kernel_text(" ============ #\n\n");
}

// ====== colore soltanto sulle etichette e ripristino prima del messaggio ====== //

int kernel_log(unsigned int level, const char *message)
{
    const char *labels[] = {"\x1b[32m[  OK  ]", "\x1b[34m[ INFO ]", "\x1b[33m[ WARN ]", "\x1b[31m[ FAIL ]"};
    if (level > 3) return 0;
    return kernel_text(labels[level]) && kernel_text("\x1b[0m ") && kernel_text(message) && kernel_text("\n");
}

int kernel_number(BOOT_U64 value)
{
    char number[19] = "0x0000000000000000";
    unsigned int index;
    for (index = 0; index < 16; ++index) {
        number[17 - index] = "0123456789abcdef"[value & 15];
        value >>= 4;
    }
    return kernel_text(number);
}

// ====== panic terminale anche quando la seriale smette di rispondere ====== //

__declspec(noreturn) void kernel_panic(const char *message)
{
    kernel_section("kernel panic");
    kernel_log(3, message);
    x86_kernel_stop(0x4b50414e49430001ULL);
}
