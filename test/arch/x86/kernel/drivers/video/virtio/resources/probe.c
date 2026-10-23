#include "probe.h"
#include "test/arch/x86/kernel/drivers/video/virtio/queues/probe.h"
#include "arch/x86/mm/window/access.h"
#include "kernel/diagnostics/log.h"
#ifdef KERNEL_TEST_VGPU_SCANOUT
#include "presentation/visual.h"
#endif
#ifdef KERNEL_TEST_VGPU_IOMMU
#include "test/arch/x86/kernel/drivers/video/virtio/iommu/probe.h"
#endif
static X86_SURFACE_MAPPING surface;
static X86_VGPU_QUEUE_TRANSPORT native;
static VGPU_QUEUE queue;
static VGPU_RESOURCE resource;

// ====== report_private ======

static int report_private(const char *label,BOOT_U64 value)
{
    return kernel_text(label) && kernel_number(value) && kernel_text("\n");
}

// ====== device_private ======

// Device-visible addresses: identity without an IOMMU; with V1 the queue lease is parked so the
// window can serve the VT-d borrows, the GPU domain is enabled and the lease is mapped again.
static int device_private(X86_VGPU_MAPPING *owner,VGPU_BUFFERS *buffers,uint64_t *backing,KERNEL_PHYSICAL_LEASE *parked)
{
#ifdef KERNEL_TEST_VGPU_IOMMU
    if (!x86_page_window_park(owner->Window,parked) ||
        !x86_vgpu_iommu_enable(parked,&surface.Authority.Data) ||
        !x86_page_window_resume(owner->Window,parked)) return 0;
    for (uint32_t index=0; index<4; ++index) {
        buffers->Device[index]=x86_vgpu_iommu_device(buffers->Physical[index],4096,3);
        if (!buffers->Device[index]) return 0;
    }
    *backing=x86_vgpu_iommu_device(surface.Authority.Data.Base,surface.Authority.Bytes,1);
    return *backing && report_private("KERNEL_VGPU_IOMMU_BACKING: ",*backing);
#else
    (void)owner; (void)parked;
    for (uint32_t index=0; index<4; ++index) buffers->Device[index]=buffers->Physical[index];
    *backing=surface.Authority.Data.Base;
    return 1;
#endif
}

// ====== exercise_private ======

static int exercise_private(uint64_t backing,uint32_t width,uint32_t height)
{
#ifdef KERNEL_TEST_VGPU_SCANOUT
    (void)width; (void)height;
    return x86_vgpu_visual_probe(&queue,&resource,&surface,backing);
#else
    if (!kernel_vgpu_resource_run(&queue,&resource,surface.Authority.Data.Base,backing,width,height)) return 0;
#ifdef KERNEL_TEST_VGPU_IOMMU_FAULT
    // Negative V1: ATTACH_BACKING of a revoked backing. QEMU maps the backing at attach time, so the
    // translation fails there: the GPU records a read fault (FR 6) on the first backing page.
    KERNEL_PHYSICAL_LEASE parked={0};
    X86_VGPU_MAPPING *gpu=surface.Gpu;
    if (!x86_vgpu_iommu_quiet() || !x86_page_window_park(gpu->Window,&parked) ||
        !gpu->Capture(gpu->Context,gpu->Window->Pool,gpu->Lapic,gpu->Inventory) ||
        !x86_vgpu_iommu_revoke(X86_VGPU_IOMMU_SURFACE) || !x86_page_window_resume(gpu->Window,&parked) ||
        !gpu->Capture(gpu->Context,gpu->Window->Pool,gpu->Lapic,gpu->Inventory)) return 0;
    VGPU_RESOURCE revoked={0};
    if (kernel_vgpu_resource_open(&queue,&revoked,surface.Authority.Data.Base,backing,width,height)) return 0;
    return report_private("KERNEL_VGPU_IOMMU_REVOKED_STATE: ",revoked.State) && x86_vgpu_iommu_expect(6,1,backing) &&
        kernel_log(0,"KERNEL_VGPU_IOMMU_REVOKE_BLOCKED");
#else
    return 1;
#endif
#endif
}

// ====== x86_vgpu_resource_probe ======

int x86_vgpu_resource_probe(X86_VGPU_MAPPING *owner,X86_VGPU_TRANSPORT *transport,const KERNEL_VGPU_IO *io,
    const KERNEL_ACPI_DIRECTORY *directory)
{
    uint32_t width,height,acknowledged=0;
    uint64_t backing=0;
    VGPU_QUEUE_IO_CONTEXT access={0};
    VGPU_BUFFERS buffers={0};
    KERNEL_VGPU_NEGOTIATION result={0};
    KERNEL_PHYSICAL_LEASE parked={0};
    KERNEL_VGPU_IO negotiation=*io;
    BOOT_U64 budget=owner->Window->Pool->FreePages;
    int passed=0, acquired=0;
#ifdef KERNEL_TEST_VGPU_IOMMU
    negotiation.Platform=1;
    int translated=0;
#else
    (void)directory;
#endif
    if (!x86_vgpu_queue_dimensions(&width,&height) || !x86_vgpu_surface_bind(owner,width,height,&surface)) return 0;
    volatile uint32_t *pixels=x86_vgpu_surface_pixels(&surface);
    if (!pixels) goto cleanup;
    for (BOOT_U64 index=0; index<surface.Authority.Data.Pages*1024; ++index)
        pixels[index]=index<surface.Authority.Bytes/4 ? 0xff000000U|((uint32_t)index*2654435761U&0xffffffU):0;
    if (!report_private("KERNEL_VGPU_RESOURCE_WIDTH: ",width) || !report_private("KERNEL_VGPU_RESOURCE_HEIGHT: ",height) ||
        !report_private("KERNEL_VGPU_RESOURCE_RAM: ",surface.Authority.Data.Base) ||
        !report_private("KERNEL_VGPU_RESOURCE_BYTES: ",surface.Authority.Bytes) ||
        !report_private("KERNEL_VGPU_RESOURCE_TABLES: ",surface.Authority.Count)) goto cleanup;
    for (uint32_t index=0; index<surface.Authority.Count; ++index)
        if (!report_private("KERNEL_VGPU_RESOURCE_TABLE: ",surface.Authority.Tables[index].Base)) goto cleanup;
#ifdef KERNEL_TEST_VGPU_IOMMU
    if (!x86_vgpu_iommu_route(owner,directory)) goto cleanup;
    translated=1;
#endif
    if (!x86_vgpu_queue_open(transport,&native,&access) || !kernel_vgpu_negotiate(&negotiation,&result) ||
        !report_private("KERNEL_VGPU_RESOURCE_FEATURES: ",result.Accepted)) goto cleanup;
    if (!x86_page_window_acquire(owner->Window,4)) goto cleanup;
    acquired=1;
    volatile unsigned char *bytes=x86_window_bytes();
    if (!bytes || !kernel_physical_is_pinned(owner->Window->Pool,&owner->Window->Lease)) goto cleanup;
    buffers.Bits=owner->Authority.PhysicalBits;
    for (uint32_t index=0; index<4; ++index) {
        buffers.Views[index]=bytes+4096ULL*index;
        buffers.Physical[index]=owner->Window->Lease.Base+4096ULL*index;
    }
    if (!device_private(owner,&buffers,&backing,&parked)) {
        acquired=owner->Window->Active!=0;
        goto cleanup;
    }
    if (!kernel_vgpu_queue_start(&queue,&access,&buffers) || !kernel_vgpu_queue_exchange(&queue) ||
        queue.Width!=width || queue.Height!=height) goto cleanup;
    if (!exercise_private(backing,width,height)) goto cleanup;
    if (!x86_vgpu_queue_register(&native,2,0,1,0,&acknowledged) || (acknowledged&~3U)) goto cleanup;
#ifdef KERNEL_TEST_VGPU_IOMMU_FAULT
    passed=!queue.Fault || queue.Fault==VGPU_QUEUE_RESPONSE;
#else
#ifdef KERNEL_TEST_VGPU_IOMMU
    // The inventory is captured again only after VT-d is released: the owned register mapping
    // verifies the snapshot it was bound with.
    passed=resource.State==5 && !resource.Bound && !queue.Fault && x86_vgpu_iommu_quiet();
#else
    passed=resource.State==5 && !resource.Bound && !queue.Fault &&
        owner->Capture(owner->Context,owner->Window->Pool,owner->Lapic,owner->Inventory);
#endif
#endif
cleanup:
    if (!report_private("KERNEL_VGPU_RESOURCE_STATE: ",resource.State) ||
        !report_private("KERNEL_VGPU_RESOURCE_COUNT: ",queue.Sequence) ||
        !report_private("KERNEL_VGPU_RESOURCE_FAULT: ",queue.Fault)) return 0;
    if (!kernel_vgpu_reset(io,&result)) return 0;
    queue.State=0; native.Self=0;
#ifdef KERNEL_TEST_VGPU_IOMMU
    // The device is reset: no DMA can be in flight. Park the queue lease so VT-d can borrow the
    // window, turn translation off, then free the lease that the domain no longer names.
    // The paging snapshot records the window state: capture it again after parking, before VT-d
    // verifies it in the teardown.
    if (acquired && (!x86_page_window_park(owner->Window,&parked) ||
        !owner->Capture(owner->Context,owner->Window->Pool,owner->Lapic,owner->Inventory))) return 0;
    acquired=0;
    if (translated && !x86_vgpu_iommu_disable()) return 0;
    if (parked.Pages && (!kernel_physical_pin(owner->Window->Pool,&parked,0) ||
        !kernel_physical_release(owner->Window->Pool,&parked))) return 0;
    if (passed && !owner->Capture(owner->Context,owner->Window->Pool,owner->Lapic,owner->Inventory)) passed=0;
#else
    (void)parked;
#endif
    if (acquired && !x86_page_window_release(owner->Window)) return 0;
    if (!x86_vgpu_surface_unbind(&surface) || x86_surface_owner() || owner->Window->Pool->FreePages!=budget) return 0;
    return kernel_log(passed?0:3,passed?"KERNEL_VGPU_RESOURCE_OK":"KERNEL_VGPU_RESOURCE_FAILED") && passed;
}
