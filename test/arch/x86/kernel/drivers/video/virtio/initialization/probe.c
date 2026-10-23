#include "probe.h"
#include "kernel/diagnostics/log.h"
#ifdef KERNEL_TEST_VGPU_RESOURCES
#include "test/arch/x86/kernel/drivers/video/virtio/resources/probe.h"
#endif
#ifdef KERNEL_TEST_VGPU_QUEUES
#include "test/arch/x86/kernel/drivers/video/virtio/queues/probe.h"
#endif
static X86_VGPU_MAPPING owner;

// ====== x86_vgpu_initialization_probe ======

int x86_vgpu_initialization_probe(const KERNEL_BOOT_CONTEXT *context,X86_PAGE_WINDOW *window,
    const X86_LAPIC_MAPPING *lapic,X86_PAGING_INVENTORY *inventory,const KERNEL_ACPI_DIRECTORY *directory)
{
    X86_VGPU_TRANSPORT transport={0};
    KERNEL_VGPU_IO io={0};
    KERNEL_VGPU_NEGOTIATION result={0};
    BOOT_U64 budget=window->Pool->FreePages;
    int negotiated, reset;
    if (!kernel_section("VirtIO GPU reset e negoziazione diagnostica") ||
        !kernel_text("KERNEL_VGPU_INIT_ROOT: ") || !kernel_number(context->Info.PageTableRoot) ||
        !kernel_text("\nKERNEL_VGPU_INIT_BUDGET_BEFORE: ") || !kernel_number(budget) || !kernel_text("\n")) return 0;
    if (!context->Info.Gpu.Present) return kernel_text("KERNEL_VGPU_INIT_BUDGET_AFTER: ") &&
        kernel_number(window->Pool->FreePages) && kernel_text("\n") && kernel_log(1,"KERNEL_VGPU_INIT_ABSENT");
    if (!x86_vgpu_bind(context,window,lapic,inventory,x86_paging_inventory_capture_lapic,&owner)) {
        (void)kernel_log(3,"KERNEL_VGPU_INIT_BIND_FAILED"); return 0;
    }
    if (!kernel_log(0,"KERNEL_VGPU_INIT_MAPPING_OK")) return 0;
    if (!x86_vgpu_transport_open(&owner,directory,&transport,&io)) {
        (void)kernel_log(3,"KERNEL_VGPU_INIT_TRANSPORT_FAILED");
        (void)x86_vgpu_unbind(&owner); return 0;
    }
    if (!kernel_text("KERNEL_VGPU_INIT_COMMON: ") || !kernel_number(transport.Base) ||
        !kernel_text("\nKERNEL_VGPU_INIT_PHYSICAL: ") ||
        !kernel_number((owner.Authority.Plan.Physical[owner.Authority.Plan.Offset[0]/4096])+
            (owner.Authority.Plan.Offset[0]&4095)) || !kernel_text("\n")) return 0;
#ifdef KERNEL_TEST_VGPU_IOMMU
    // V1: ACCESS_PLATFORM is accepted from the first negotiation; no DMA exists before the queues,
    // which start only after the GPU domain translates.
    io.Platform=1;
#endif
    negotiated=kernel_vgpu_negotiate(&io,&result);
#ifdef KERNEL_TEST_VGPU_QUEUES
    if (negotiated && !x86_vgpu_queue_probe(&owner,&transport,&io)) negotiated=0;
#endif
#ifdef KERNEL_TEST_VGPU_RESOURCES
    if (negotiated && !x86_vgpu_resource_probe(&owner,&transport,&io,directory)) negotiated=0;
#endif
    if (!negotiated && !result.Reset) {
        uint32_t failed=0;
        if (io.Register(io.Context,20,1,0,&failed) && failed && failed<=15) {
            failed |= 128;
            (void)io.Register(io.Context,20,1,1,&failed);
        }
    }
    if (!kernel_text("KERNEL_VGPU_INIT_OFFERED: ") || !kernel_number(result.Offered) ||
        !kernel_text("\nKERNEL_VGPU_INIT_ACCEPTED: ") || !kernel_number(result.Accepted) ||
        !kernel_text("\nKERNEL_VGPU_INIT_STATUS: ") || !kernel_number(result.Status) ||
        !kernel_text("\nKERNEL_VGPU_INIT_FAULT: ") || !kernel_number(result.Fault) ||
        !kernel_text("\n")) return 0;
    reset=kernel_vgpu_reset(&io,&result);
    if (!reset || !x86_vgpu_transport_end(&transport) || !x86_vgpu_unbind(&owner) ||
        window->Pool->FreePages!=budget || x86_paging_gpu_owner()) return 0;
    return kernel_text("KERNEL_VGPU_INIT_BUDGET_AFTER: ") && kernel_number(window->Pool->FreePages) && kernel_text("\n") &&
        kernel_log(negotiated?0:3,negotiated?"KERNEL_VGPU_INIT_OK":"KERNEL_VGPU_INIT_REJECTED") && negotiated;
}
