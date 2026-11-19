#include "log.h"
#ifdef KERNEL_PS2_INPUT
#include "kernel/scheduler/preempt.h"
#endif

static volatile unsigned int line_open;

// ====== testi statici limitati e nessuna dipendenza da printf ====== //

int kernel_text(const char *text)
{
    unsigned int index;
    if (!text) return 0;
    for (index = 0; index < 256; ++index) {
        if (!text[index]) return 1;
        unsigned int was_open = line_open;
        line_open = 1;
        if ((text[index] == '\n' && !kernel_log_character('\r')) ||
            !kernel_log_character(text[index])) {
            line_open = was_open;
            return 0;
        }
        line_open = text[index] != '\n';
#ifdef KERNEL_PS2_INPUT
        if (!line_open) kernel_preempt_log_boundary();
#endif
    }
    return 0;
}

// ====== kernel_log_line_open ======

int kernel_log_line_open(void)
{
    return line_open != 0;
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

__declspec(noreturn) __declspec(noinline) static void panic_legacy_private(const char *message) {
    kernel_section("kernel panic");
    kernel_log(3, message);
    kernel_log_stop(0x4b50414e49430001ULL);
}

// ====== kernel_panic ======

__declspec(noreturn) void kernel_panic(const char *message) {
    kernel_log_panic_prepare(message);
    panic_legacy_private(message);
}
