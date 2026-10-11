#include "arch/x86/kernel/platform.h"
static unsigned char ports[8];
static unsigned int serial_mode, output_count;
static char output[512];

// ====== UART modellata solo per le operazioni usate dalla diagnostica ====== //

void mock_serial_mode(unsigned int mode)
{
    unsigned int index;
    serial_mode = mode;
    output_count = 0;
    for (index = 0; index < 8; ++index) ports[index] = 0;
}
unsigned char x86_read_port(unsigned short port)
{
    if (port < 0x3f8 || port > 0x3ff || serial_mode == 1) return 255;
    if (port == 0x3fd) return serial_mode == 2 ? 0 : 32;
    return ports[port - 0x3f8];
}
void x86_write_port(unsigned short port, unsigned char value)
{
    if (port < 0x3f8 || port > 0x3ff) return;
    if (port == 0x3f8 && !(ports[3] & 128) && output_count < sizeof(output)) output[output_count++] = (char)value;
    ports[port - 0x3f8] = value;
}
unsigned int mock_serial_writes(void) { return output_count; }

// ====== reset ANSI subito dopo etichetta e prima del testo ====== //

int mock_serial_colors(void)
{
    const char expected[] = "\x1b[32m[  OK  ]\x1b[0m successo\r\n\x1b[31m[ FAIL ]\x1b[0m errore\r\n";
    unsigned int offset, index;
    for (offset = 0; offset + sizeof(expected) - 1 <= output_count; ++offset) {
        for (index = 0; index < sizeof(expected) - 1; ++index) if (output[offset + index] != expected[index]) break;
        if (index == sizeof(expected) - 1) return 1;
    }
    return 0;
}
__declspec(noreturn) void x86_kernel_stop(BOOT_U64 marker)
{
    (void)marker;
    for (;;) { }
}
