#ifndef OS_X86_KERNEL_DRIVERS_NET_VIRTIO_NET_NATIVE_H
#define OS_X86_KERNEL_DRIVERS_NET_VIRTIO_NET_NATIVE_H

#include "arch/x86/kernel/drivers/bus/pci/ecam/ecam.h"
#include "arch/x86/kernel/timer/hpet/watchdog/watchdog.h"
#include "arch/x86/kernel/timer/lapic/timer.h"
#include "kernel/drivers/net/virtio_net/virtio_net.h"

int x86_virtio_net_start(
    const X86_WATCHDOG_ENV *env,
    const KERNEL_ACPI_DIRECTORY *directory,
    const X86_PM_CLOCK *clock
);
void x86_virtio_net_stop(void);

KERNEL_NET_DEVICE *x86_virtio_net_device(void);

#endif
