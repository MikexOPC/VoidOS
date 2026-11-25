#ifndef OS_X86_KERNEL_DRIVERS_STORAGE_AHCI_NATIVE_H
#define OS_X86_KERNEL_DRIVERS_STORAGE_AHCI_NATIVE_H

#include "arch/x86/kernel/drivers/bus/pci/ecam/ecam.h"
#include "arch/x86/kernel/timer/hpet/watchdog/watchdog.h"
#include "arch/x86/kernel/timer/lapic/timer.h"
#include "kernel/drivers/storage/ahci/ahci.h"
#include "kernel/storage/cache.h"

int x86_ahci_start(
    const X86_WATCHDOG_ENV *env,
    const KERNEL_ACPI_DIRECTORY *directory,
    const X86_PM_CLOCK *clock
);
void x86_ahci_stop(void);

KERNEL_BLOCK_DEVICE *x86_ahci_device(void);
KERNEL_BLOCK_CACHE  *x86_ahci_cache(void);

#endif
