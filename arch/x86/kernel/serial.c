#include "platform.h"
#define COM1 0x3f8U
static int serial_ready;

// ====== UART COM1 del profilo QEMU con interrupt disabilitati ====== //

int x86_serial_initialize(void)
{
    unsigned char scratch;
    serial_ready = 0;
    if (x86_read_port(COM1 + 5) == 255) return 0;
    scratch = x86_read_port(COM1 + 7);
    x86_write_port(COM1 + 7, 0x5a);
    if (x86_read_port(COM1 + 7) != 0x5a) { x86_write_port(COM1 + 7, scratch); return 0; }
    x86_write_port(COM1 + 7, scratch);
    x86_write_port(COM1 + 3, 0);
    x86_write_port(COM1 + 1, 0);
    x86_write_port(COM1 + 3, 0x80);
    x86_write_port(COM1, 1);
    x86_write_port(COM1 + 1, 0);
    x86_write_port(COM1 + 3, 3);
    x86_write_port(COM1 + 2, 7);
    x86_write_port(COM1 + 4, 3);
    if (x86_read_port(COM1 + 3) != 3) return 0;
    serial_ready = 1;
    return 1;
}

// ====== attesa limitata invece di blocco infinito sul trasmettitore ====== //

int x86_serial_character(char value)
{
    unsigned int attempt;
    if (!serial_ready) return 0;
    for (attempt = 0; attempt < 100000; ++attempt) {
        unsigned char status = x86_read_port(COM1 + 5);
        if (status == 255) break;
        if (status & 0x20) { x86_write_port(COM1, (unsigned char)value); return 1; }
    }
    serial_ready = 0;
    return 0;
}
