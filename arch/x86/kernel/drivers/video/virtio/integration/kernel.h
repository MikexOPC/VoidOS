#ifndef X86_VGPU_KERNEL_H
#define X86_VGPU_KERNEL_H
#include "../mapping.h"
int x86_vgpu_kernel_start(const KERNEL_BOOT_CONTEXT *context, X86_PAGE_WINDOW *window,
    const X86_LAPIC_MAPPING *lapic, X86_PAGING_INVENTORY *inventory);
#endif
