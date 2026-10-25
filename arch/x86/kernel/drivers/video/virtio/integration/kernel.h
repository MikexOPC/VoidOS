#ifndef X86_VGPU_KERNEL_H
#define X86_VGPU_KERNEL_H
#include "../mapping.h"
#include "../iommu/session.h"
int x86_vgpu_kernel_start(const KERNEL_BOOT_CONTEXT *context, X86_PAGE_WINDOW *window,
    const X86_LAPIC_MAPPING *lapic, X86_PAGING_INVENTORY *inventory);
// The ordinary GPU register mapping, 0 until x86_vgpu_kernel_start bound a present GPU.
X86_VGPU_MAPPING *x86_vgpu_kernel_mapping(void);
// G6 composition root: the one V2b session object, started and stopped with the kernel mapping.
int x86_vgpu_kernel_session_start(const KERNEL_ACPI_DIRECTORY *directory);
int x86_vgpu_kernel_session_stop(void);
// The console's display: the session's operations while it translates, otherwise none (GOP).
KERNEL_DISPLAY x86_vgpu_kernel_display(void);
#endif
