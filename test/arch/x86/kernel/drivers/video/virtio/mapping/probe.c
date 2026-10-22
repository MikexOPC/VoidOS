#include "probe.h"
#include "arch/x86/kernel/drivers/video/framebuffer/integration/kernel.h"
#include "kernel/diagnostics/log.h"

// ====== x86_vgpu_mapping_probe ======

int x86_vgpu_mapping_probe(const KERNEL_BOOT_CONTEXT *context, X86_PAGE_WINDOW *window,
    const X86_LAPIC_MAPPING *lapic, X86_PAGING_INVENTORY *inventory, X86_VGPU_MAPPING *owner)
{
    BOOT_U64 budget=owner->Budget, previous=owner->Authority.Tables[0].Id;
    if (!x86_vgpu_unbind(owner) || window->Pool->FreePages != budget || x86_paging_gpu_owner() ||
        x86_fb_root()[X86_GPU_SLOT] || !x86_fb_kernel_inventory_verify(context,window->Pool,lapic,inventory) ||
        !x86_vgpu_bind(context,window,lapic,inventory,x86_fb_kernel_inventory_capture,owner) ||
        owner->Authority.Tables[0].Id==previous || window->Pool->FreePages != budget-3) return 0;
    return kernel_log(0,"KERNEL_VGPU_ROLLBACK_OK");
}
