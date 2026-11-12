#include "probe.h"
#include "arch/x86/kernel/drivers/input/ps2/runtime/session.h"
#include "arch/x86/kernel/exceptions/terminal.h"
#include "arch/x86/kernel/platform.h"
#include "arch/x86/kernel/process/process.h"
#include "kernel/diagnostics/log.h"

static X86_PS2_SESSION keyboard;
static const unsigned char bytes[] = {0x1c, 0x32};
static BOOT_U32 received;

// ====== firmware_read ======

static int firmware_read(void *context, OS_ACPI_U64 physical, OS_ACPI_U32 size, unsigned char *output)
{
    const X86_WATCHDOG_ENV *env = context;
    return x86_page_window_read_acpi(env->Window, env->Context, physical, size, output);
}

// ====== port_read ======

static int port_read(void *context, uint16_t port, uint8_t *value)
{
    (void)context;
    if (!value || (port != 0x60 && port != 0x64) || !x86_exception_environment()) return 0;
    *value = x86_read_port(port);
    return 1;
}

// ====== port_write ======

static int port_write(void *context, uint16_t port, uint8_t value)
{
    (void)context;
    if ((port != 0x60 && port != 0x64) || !x86_exception_environment()) return 0;
    x86_write_port(port, value);
    return 1;
}

// ====== clock_read ======

static int clock_read(void *context, uint32_t *ticks)
{
    const X86_PM_CLOCK *clock = context;
    if (!ticks || !clock->Ready || !x86_exception_environment()) return 0;
    *ticks = x86_pm_read((unsigned short)clock->Port) & clock->Mask;
    return 1;
}

// ====== inject ======

static int inject(void)
{
    uint8_t status;
    // QEMU v10.1.0 pckbd.c: D2 at 64h queues the next data byte from 60h as keyboard output.
    // Mode bit 0 raises IRQ1. Status OBF bit 0 and AUX/error bits 5–7 must be clear before injection.
    return received < sizeof(bytes) && port_read(0, 0x64, &status) && !(status & 0xe1) &&
        kernel_ps2_send(&keyboard.Controller, 0x64, 0xd2) &&
        kernel_ps2_send(&keyboard.Controller, 0x60, bytes[received]);
}

// ====== handle ======

static int handle(void)
{
    uint8_t value;
    if (received >= sizeof(bytes) ||
        !kernel_ps2_receive(&keyboard.Controller, &value) || value != bytes[received] ||
        !x86_lapic_eoi_vector(keyboard.Env.Mapping, 0x41)) return 0;
    ++received;
    return 1;
}

// ====== x86_k6_keyboard_probe ======

int x86_k6_keyboard_probe(const X86_WATCHDOG_ENV *env, const KERNEL_ACPI_DIRECTORY *directory,
    const X86_PM_CLOCK *clock)
{
    KERNEL_PS2_DESCRIPTION description;
    const X86_PROCESS_IMAGE image = {k6_program_hello, k6_program_hello_length};
    if (!env || !clock || !clock->Ready || !x86_lapic_idle(env->Mapping) ||
        !kernel_ps2_discover(directory, env->Window->Pool->PhysicalBits, firmware_read,
            (void *)env, &description) || !description.Present ||
        !x86_ps2_route_plan(env->Topology, env->Capabilities, env->Window->Pool->PhysicalBits,
            &keyboard.Route)) return 0;
    keyboard.Env = *env;
    keyboard.Clock = *clock;
    KERNEL_PS2_IO io = {&keyboard.Clock, port_read, port_write, clock_read, clock->Mask, 357955U};
    if (!kernel_ps2_prepare(&keyboard.Controller, &io)) {
        if (keyboard.Controller.Phase == KERNEL_PS2_DIRTY) kernel_panic("K6_PS2_CLEANUP_FAILED");
        return 0;
    }
    int active = x86_ps2_route_apply(&keyboard, 1) && kernel_ps2_start(&keyboard.Controller);
    int passed = active && x86_ps2_route_apply(&keyboard, 0) &&
        x86_process_device_irq_check(&image, inject, handle) && received == sizeof(bytes);
    if (keyboard.RouteOwned && !x86_ps2_route_apply(&keyboard, 1)) kernel_panic("K6_PS2_CLEANUP_FAILED");
    if (!(active ? kernel_ps2_stop(&keyboard.Controller) : kernel_ps2_close(&keyboard.Controller)))
        kernel_panic("K6_PS2_CLEANUP_FAILED");
    keyboard.Quiet = 1;
    if ((keyboard.RouteOwned && !x86_ps2_route_apply(&keyboard, 2)) ||
        !x86_lapic_idle(env->Mapping) || !x86_page_window_idle_valid(env->Window))
        kernel_panic("K6_PS2_CLEANUP_FAILED");
    return passed && kernel_log(0, "KERNEL_K6_EXTERNAL_IRQ_OK");
}
