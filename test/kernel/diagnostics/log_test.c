#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kernel/diagnostics/log.h"

static char written[256];
static unsigned int count, refuse_at;
static unsigned int boundaries;

// ====== serial_mock ======

int kernel_log_character(char value)
{
    assert(kernel_log_line_open());
    if (refuse_at && count + 1 == refuse_at) return 0;
    assert(count < sizeof(written) - 1);
    written[count++] = value;
    return 1;
}

__declspec(noreturn) void kernel_log_stop(BOOT_U64 marker)
{
    (void)marker;
    abort();
}

// ====== kernel_log_panic_prepare ======

void kernel_log_panic_prepare(const char *message) {
    (void)message;
}

// ====== kernel_preempt_log_boundary ======

void kernel_preempt_log_boundary(void) {
    assert(!kernel_log_line_open());
    assert(count && written[count - 1] == '\n');
    ++boundaries;
}

// ====== main ======

int main(void)
{
    assert(!kernel_log_line_open());
    assert(kernel_text("NAME: ") && kernel_log_line_open());
    assert(kernel_number(0x2a) && kernel_log_line_open());
    assert(kernel_text("\n") && !kernel_log_line_open());
#ifdef KERNEL_PS2_INPUT
    assert(boundaries == 1);
#endif
    assert(strcmp(written, "NAME: 0x000000000000002a\r\n") == 0);
    assert(kernel_text("") && !kernel_log_line_open());
    assert(kernel_log(0, "MARKER") && !kernel_log_line_open());
    assert(kernel_text("A\nB") && kernel_log_line_open());
    assert(kernel_text("\n\n") && !kernel_log_line_open());
    count = 0;
    memset(written, 0, sizeof(written));
    refuse_at = 3;
    assert(!kernel_text("XYZ\n") && kernel_log_line_open() && count == 2);
    refuse_at = 0;
    assert(kernel_text("\n") && !kernel_log_line_open());
    puts("PASS: log line state follows open and closed lines, multi-part messages and serial failure; serial MOCK");
    return 0;
}
