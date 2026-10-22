#ifndef TEST_X86_VGPU_PROBE_H
#define TEST_X86_VGPU_PROBE_H
#include "arch/x86/kernel/drivers/video/virtio/mapping.h"
int x86_vgpu_mapping_probe(const KERNEL_BOOT_CONTEXT *context, X86_PAGE_WINDOW *window,
    const X86_LAPIC_MAPPING *lapic, X86_PAGING_INVENTORY *inventory, X86_VGPU_MAPPING *owner);
#endif
