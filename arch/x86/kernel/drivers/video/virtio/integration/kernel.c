#include "kernel.h"
#include "arch/x86/kernel/drivers/video/framebuffer/integration/kernel.h"
#include "kernel/diagnostics/log.h"
#ifdef KERNEL_TEST_VGPU_MAPPING
#include "test/arch/x86/kernel/drivers/video/virtio/mapping/probe.h"
#endif
static X86_VGPU_MAPPING mapping;
// G6 composition root: the V2b/direct-DMA session object (and the present it owns) lives here.
static X86_VGPU_SESSION session;

// ====== x86_vgpu_kernel_start ======

int x86_vgpu_kernel_start(const KERNEL_BOOT_CONTEXT *context, X86_PAGE_WINDOW *window,
    const X86_LAPIC_MAPPING *lapic, X86_PAGING_INVENTORY *inventory)
{
    if (!x86_vgpu_bind(context,window,lapic,inventory,x86_fb_kernel_inventory_capture,&mapping)) return 0;
    if (!context->Info.Gpu.Present) return kernel_log(1,"KERNEL_VGPU_MAPPING_ABSENT");
#ifdef KERNEL_TEST_VGPU_MAPPING
    if (!x86_vgpu_mapping_probe(context,window,lapic,inventory,&mapping)) return 0;
#endif
    if (!kernel_text("KERNEL_VGPU_MAPPING_VIRTUAL: ") || !kernel_number(X86_GPU_VIRTUAL) ||
        !kernel_text("\nKERNEL_VGPU_MAPPING_ROOT: ") || !kernel_number(mapping.Authority.Root) ||
        !kernel_text("\nKERNEL_VGPU_MAPPING_CACHE: ") || !kernel_number(mapping.Authority.Cache) ||
        !kernel_text("\nKERNEL_VGPU_MAPPING_PAT: ") || !kernel_number(mapping.Authority.Pat) ||
        !kernel_text("\nKERNEL_VGPU_MAPPING_PAGES: ") || !kernel_number(mapping.Authority.Plan.Count) ||
        !kernel_text("\n")) return 0;
    for (BOOT_U32 index=0; index<3; ++index)
        if (!kernel_text("KERNEL_VGPU_MAPPING_TABLE: ") || !kernel_number(mapping.Authority.Tables[index].Base) || !kernel_text("\n")) return 0;
    for (BOOT_U32 index=0; index<mapping.Authority.Plan.Count; ++index)
        if (!kernel_text("KERNEL_VGPU_MAPPING_PHYSICAL: ") || !kernel_number(mapping.Authority.Plan.Physical[index]) || !kernel_text("\n")) return 0;
    return kernel_log(0,"KERNEL_VGPU_MAPPING_LIVE");
}

// ====== x86_vgpu_kernel_mapping ======

X86_VGPU_MAPPING *x86_vgpu_kernel_mapping(void)
{
    return mapping.Self == (BOOT_U64)&mapping ? &mapping : 0;
}

// ====== x86_vgpu_kernel_session_start ======

int x86_vgpu_kernel_session_start(const KERNEL_ACPI_DIRECTORY *directory)
{
    return x86_vgpu_session_start(&session, x86_vgpu_kernel_mapping(), directory);
}

// ====== x86_vgpu_kernel_session_stop ======

int x86_vgpu_kernel_session_stop(void)
{
    return x86_vgpu_session_stop(&session, x86_vgpu_kernel_mapping());
}

// ====== x86_vgpu_kernel_display ======

KERNEL_DISPLAY x86_vgpu_kernel_display(void)
{
    return x86_vgpu_session_state(&session) == X86_VGPU_SESSION_TRANSLATING ? x86_vgpu_session_display(&session) : (KERNEL_DISPLAY){0};
}
