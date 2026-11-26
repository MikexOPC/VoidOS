#ifndef OS_X86_KERNEL_DRIVERS_USB_XHCI_NATIVE_H
#define OS_X86_KERNEL_DRIVERS_USB_XHCI_NATIVE_H

#include "arch/x86/kernel/drivers/bus/pci/ecam/ecam.h"
#include "arch/x86/kernel/timer/hpet/watchdog/watchdog.h"
#include "arch/x86/kernel/timer/lapic/timer.h"
#include "kernel/drivers/usb/xhci/xhci.h"

int x86_xhci_start(
    const X86_WATCHDOG_ENV *env,
    const KERNEL_ACPI_DIRECTORY *directory,
    const X86_PM_CLOCK *clock
);
uint32_t x86_xhci_poll(void);
int x86_xhci_has_shell_scalar(void);
int x86_xhci_pop_shell_scalar(uint32_t *scalar_out);
void x86_xhci_stop(void);
int x86_xhci_ready(void);

#endif
