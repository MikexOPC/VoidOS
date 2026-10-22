#ifndef TEST_X86_VGPU_INITIALIZATION_H
#define TEST_X86_VGPU_INITIALIZATION_H
#include "arch/x86/kernel/drivers/video/virtio/initialization/transport.h"
int x86_vgpu_initialization_probe(const KERNEL_BOOT_CONTEXT *context,X86_PAGE_WINDOW *window,
    const X86_LAPIC_MAPPING *lapic,X86_PAGING_INVENTORY *inventory,const KERNEL_ACPI_DIRECTORY *directory);
#endif
