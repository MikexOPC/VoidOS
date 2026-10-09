#include "native.h"
#include "kernel/drivers/input/ps2/controller/controller.h"
#include "arch/x86/kernel/timer/lapic/timer.h"
#include "arch/x86/kernel/exceptions/terminal.h"
#include "kernel/diagnostics/log.h"

typedef struct {
    const KERNEL_BOOT_CONTEXT *Context;
    X86_PAGE_WINDOW           *Window;
    X86_PM_CLOCK               Clock;
} PS2_NATIVE;

// ====== ps2_firmware_read ======

static int ps2_firmware_read(void *context, OS_ACPI_U64 physical, OS_ACPI_U32 bytes,
    unsigned char *output)
{
    PS2_NATIVE *native = context;
    return x86_page_window_read_acpi(native->Window, native->Context, physical, bytes, output);
}

// ====== ps2_port_read ======

static int ps2_port_read(void *context, uint16_t port, uint8_t *value)
{
    (void)context;
    /* QEMU pckbd.c i8042_realizefn: byte ports60(data),64(status).
       Caller has FADT8042 presence and exclusive BSP/IF=0 ownership. */
    if (!value || (port != 0x60 && port != 0x64) || !x86_exception_environment()) return 0;
    *value = x86_read_port(port);
    return 1;
}

// ====== ps2_port_write ======

static int ps2_port_write(void *context, uint16_t port, uint8_t value)
{
    (void)context;
    /* Same byte-width ports; no A20, reset or output-port writes. */
    if ((port != 0x60 && port != 0x64) || !x86_exception_environment()) return 0;
    x86_write_port(port, value);
    return 1;
}

// ====== ps2_clock_read ======

static int ps2_clock_read(void *context, uint32_t *ticks)
{
    PS2_NATIVE *native = context;
    /* Existing ACPI PM timer backend; capture validates GAS/legacy port,
       mask and FADT checksum. ACPI6.5 §4.8.3.3: 3.579545MHz counter. */
    if (!ticks || !native->Clock.Ready || !x86_exception_environment()) return 0;
    *ticks = x86_pm_read((unsigned short)native->Clock.Port) & native->Clock.Mask;
    return 1;
}

// ====== x86_ps2_controller_probe ======

int x86_ps2_controller_probe(const KERNEL_BOOT_CONTEXT *context, X86_PAGE_WINDOW *window,
    const KERNEL_ACPI_DIRECTORY *directory)
{
    PS2_NATIVE native = {0};
    KERNEL_PS2_DESCRIPTION description;
    KERNEL_PS2_CONTROLLER owner = {0};
    KERNEL_PS2_IO io;
    BOOT_U64 free_pages;
    if (!context || !window || !window->Pool || !x86_page_window_idle_valid(window) ||
        !x86_exception_environment()) return 0;
    native.Context = context; native.Window = window;
    free_pages = window->Pool->FreePages;
    if (!kernel_ps2_discover(directory, window->Pool->PhysicalBits, ps2_firmware_read,
        &native, &description)) return 0;
    if (!description.Present) return x86_page_window_idle_valid(window) &&
        window->Pool->FreePages == free_pages && kernel_log(2, "KERNEL_PS2_CONTROLLER_ABSENT");
    if (!x86_pm_clock_capture(directory, window->Pool->PhysicalBits, ps2_firmware_read,
        &native, &native.Clock)) return 0;
    io.Context = &native; io.Read = ps2_port_read; io.Write = ps2_port_write;
    io.Clock = ps2_clock_read; io.Mask = native.Clock.Mask; io.Timeout = 357955U;
    if (!kernel_ps2_prepare(&owner, &io)) {
        kernel_text("PS2_ERROR: "); kernel_number(owner.Error); kernel_text("\n");
        return 0;
    }
    if (!kernel_ps2_close(&owner) || owner.Error || !x86_page_window_idle_valid(window) ||
        window->Pool->FreePages != free_pages) return 0;
    return kernel_log(0, "KERNEL_PS2_CONTROLLER_SET2_OK");
}
