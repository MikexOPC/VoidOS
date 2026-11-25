#ifndef OS_X86_KERNEL_DRIVERS_STORAGE_VIRTIO_BLK_NATIVE_H
#define OS_X86_KERNEL_DRIVERS_STORAGE_VIRTIO_BLK_NATIVE_H

#include "arch/x86/kernel/drivers/bus/pci/ecam/ecam.h"
#include "arch/x86/kernel/timer/hpet/watchdog/watchdog.h"
#include "arch/x86/kernel/timer/lapic/timer.h"
#include "kernel/drivers/storage/virtio_blk/virtio_blk.h"
#include "kernel/storage/cache.h"

int x86_virtio_blk_start(
    const X86_WATCHDOG_ENV *env,
    const KERNEL_ACPI_DIRECTORY *directory,
    const X86_PM_CLOCK *clock
);
void x86_virtio_blk_stop(void);

KERNEL_BLOCK_DEVICE *x86_virtio_blk_device(void);
KERNEL_BLOCK_CACHE  *x86_virtio_blk_cache(void);

#endif
