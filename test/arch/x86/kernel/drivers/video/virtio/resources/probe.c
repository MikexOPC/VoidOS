#include "probe.h"
#include "test/arch/x86/kernel/drivers/video/virtio/queues/probe.h"
#include "arch/x86/mm/window/access.h"
#include "kernel/diagnostics/log.h"
#ifdef KERNEL_TEST_VGPU_SCANOUT
#include "presentation/visual.h"
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

// ====== x86_vgpu_resource_probe ======

int x86_vgpu_resource_probe(X86_VGPU_MAPPING *owner,X86_VGPU_TRANSPORT *transport,const KERNEL_VGPU_IO *io)
{
    uint32_t width,height,acknowledged=0;
    VGPU_QUEUE_IO_CONTEXT access={0};
    VGPU_BUFFERS buffers={0};
    KERNEL_VGPU_NEGOTIATION result={0};
    BOOT_U64 budget=owner->Window->Pool->FreePages;
    int passed=0, acquired=0;
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
    if (!x86_vgpu_queue_open(transport,&native,&access) || !kernel_vgpu_negotiate(io,&result)) goto cleanup;
    if (!x86_page_window_acquire(owner->Window,4)) goto cleanup;
    acquired=1;
    volatile unsigned char *bytes=x86_window_bytes();
    if (!bytes || !kernel_physical_is_pinned(owner->Window->Pool,&owner->Window->Lease)) goto cleanup;
    buffers.Bits=owner->Authority.PhysicalBits;
    for (uint32_t index=0; index<4; ++index) {
        buffers.Views[index]=bytes+4096ULL*index;
        buffers.Physical[index]=owner->Window->Lease.Base+4096ULL*index;
    }
    if (!kernel_vgpu_queue_start(&queue,&access,&buffers) || !kernel_vgpu_queue_exchange(&queue) ||
        queue.Width!=width || queue.Height!=height) goto cleanup;
#ifdef KERNEL_TEST_VGPU_SCANOUT
    if (!x86_vgpu_visual_probe(&queue,&resource,&surface)) goto cleanup;
#else
    if (!kernel_vgpu_resource_run(&queue,&resource,surface.Authority.Data.Base,width,height)) goto cleanup;
#endif
    if (!x86_vgpu_queue_register(&native,2,0,1,0,&acknowledged) || (acknowledged&~3U)) goto cleanup;
    passed=resource.State==5 && !resource.Bound && !queue.Fault &&
        owner->Capture(owner->Context,owner->Window->Pool,owner->Lapic,owner->Inventory);
cleanup:
    if (!report_private("KERNEL_VGPU_RESOURCE_STATE: ",resource.State) ||
        !report_private("KERNEL_VGPU_RESOURCE_COUNT: ",queue.Sequence) ||
        !report_private("KERNEL_VGPU_RESOURCE_FAULT: ",queue.Fault)) return 0;
    if (!kernel_vgpu_reset(io,&result)) return 0;
    queue.State=0; native.Self=0;
    if (acquired && !x86_page_window_release(owner->Window)) return 0;
    if (!x86_vgpu_surface_unbind(&surface) || x86_surface_owner() || owner->Window->Pool->FreePages!=budget) return 0;
    return kernel_log(passed?0:3,passed?"KERNEL_VGPU_RESOURCE_OK":"KERNEL_VGPU_RESOURCE_FAILED") && passed;
}
