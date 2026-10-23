#include "probe.h"
#include "kernel/diagnostics/log.h"
static VGPU_QUEUE queue;
static X86_VGPU_QUEUE_TRANSPORT native;
static X86_DMA_ARENA arena;

// ====== x86_vgpu_queue_buffers ======

int x86_vgpu_queue_buffers(X86_VGPU_MAPPING *owner,X86_DMA_ARENA *dma,VGPU_BUFFERS *buffers)
{
    if (!owner || !dma || !buffers ||
        !x86_dma_arena_bind(owner->Context,owner->Window,owner->Lapic,owner->Inventory,owner->Capture,4,dma)) return 0;
    volatile unsigned char *bytes=x86_dma_arena_bytes(dma);
    if (!bytes) return 0;
    *buffers=(VGPU_BUFFERS){0};
    buffers->Bits=owner->Authority.PhysicalBits;
    for (uint32_t index=0; index<4; ++index) {
        buffers->Views[index]=bytes+4096ULL*index;
        buffers->Physical[index]=dma->Authority.Data.Base+4096ULL*index;
        buffers->Device[index]=buffers->Physical[index]; // no IOMMU: identity
    }
    return 1;
}

// ====== x86_vgpu_queue_dimensions ======

int x86_vgpu_queue_dimensions(uint32_t *width,uint32_t *height)
{
    if (!width || !height || queue.Sequence!=256 || queue.State || queue.Fault || !queue.Width || !queue.Height) return 0;
    *width=queue.Width; *height=queue.Height;
    return 1;
}

// ====== x86_vgpu_queue_probe ======

int x86_vgpu_queue_probe(X86_VGPU_MAPPING *owner,X86_VGPU_TRANSPORT *transport,const KERNEL_VGPU_IO *io)
{
    VGPU_QUEUE_IO_CONTEXT access={0};
    VGPU_BUFFERS buffers={0};
    KERNEL_VGPU_NEGOTIATION reset={0};
    uint32_t acknowledged=0;
    int passed=0;
    if (!x86_vgpu_queue_open(transport,&native,&access)) return kernel_log(3,"KERNEL_VGPU_QUEUE_OPEN_FAILED") && 0;
    BOOT_U64 budget=owner->Window->Pool->FreePages;
    if (!x86_vgpu_queue_buffers(owner,&arena,&buffers)) goto cleanup;
    if (!kernel_text("KERNEL_VGPU_QUEUE_RAM: ") || !kernel_number(arena.Authority.Data.Base) || !kernel_text("\n")) goto cleanup;
    if (!kernel_vgpu_queue_start(&queue,&access,&buffers)) goto cleanup;
    for (uint32_t request=0; request<256; ++request) {
        if (!kernel_vgpu_queue_exchange(&queue) ||
            !kernel_physical_is_pinned(owner->Window->Pool,&arena.Authority.Data)) goto cleanup;
    }
    if (!x86_vgpu_queue_register(&native,2,0,1,0,&acknowledged) || (acknowledged&~3U)) goto cleanup;
    passed=queue.Sequence==256 && queue.State==1 && !queue.Fault &&
        owner->Capture(owner->Context,owner->Window->Pool,owner->Lapic,owner->Inventory);
cleanup:
    if (!kernel_text("KERNEL_VGPU_QUEUE_COUNT: ") || !kernel_number(queue.Sequence) ||
        !kernel_text("\nKERNEL_VGPU_QUEUE_FAULT: ") || !kernel_number(queue.Fault) ||
        !kernel_text("\nKERNEL_VGPU_QUEUE_SIZE: ") || !kernel_number(queue.Width) || !kernel_text("x") ||
        !kernel_number(queue.Height) || !kernel_text("\n")) return 0;
    if (!kernel_vgpu_reset(io,&reset)) return 0;
    queue.State=0; native.Self=0;
    if ((arena.Self && !x86_dma_arena_unbind(&arena)) || !x86_page_window_idle_valid(owner->Window) ||
        owner->Window->Pool->FreePages!=budget || !x86_vgpu_transport_end(transport)) return 0;
    return kernel_log(passed?0:3,passed?"KERNEL_VGPU_QUEUE_OK":"KERNEL_VGPU_QUEUE_FAILED") && passed;
}
