#include "visual.h"
#include "kernel/diagnostics/log.h"

// ====== checkpoint_private ======

static int checkpoint_private(VGPU_QUEUE *queue,uint32_t phase)
{
    volatile uint32_t *mailbox=(volatile uint32_t *)(queue->Buffers.Views[1]+4092);
    uint32_t start,now;
    const uint32_t deadline=10738635;
    if (queue->Io.Mask<deadline || !queue->Io.Clock(queue->Io.Context,&start) || start>queue->Io.Mask ||
        !kernel_text("KERNEL_VGPU_FRAME_READY: ") || !kernel_number(phase) || !kernel_text("\n")) return 0;
    for (uint32_t sample=0; sample<10000000; ++sample) {
        if (*mailbox==phase) return 1;
        if (*mailbox>phase || !queue->Io.Clock(queue->Io.Context,&now) || now>queue->Io.Mask ||
            ((now-start)&queue->Io.Mask)>=deadline) return 0;
    }
    return 0;
}

// ====== color_private ======

static uint32_t color_private(uint32_t x,uint32_t y)
{
    return 0xff000000U | (((x/32)&255)<<16) | (((y/16)&255)<<8) | ((x^y)&255);
}

// ====== x86_vgpu_visual_probe ======

int x86_vgpu_visual_probe(VGPU_QUEUE *queue,VGPU_RESOURCE *resource,X86_SURFACE_MAPPING *surface)
{
    volatile uint32_t *pixels=x86_vgpu_surface_pixels(surface);
    if (!pixels || queue->Width<128 || queue->Height<96) return 0;
    *(volatile uint32_t *)(queue->Buffers.Views[1]+4092)=0;
    for (uint32_t y=0; y<queue->Height; ++y) for (uint32_t x=0; x<queue->Width; ++x)
        pixels[(uint64_t)y*queue->Width+x]=color_private(x,y);
    if (!kernel_text("KERNEL_VGPU_SCANOUT_MAILBOX: ") || !kernel_number(queue->Buffers.Physical[1]+4092) ||
        !kernel_text("\nKERNEL_VGPU_SCANOUT_ID: ") || !kernel_number(queue->Scanout) || !kernel_text("\n") ||
        !kernel_vgpu_resource_open(queue,resource,surface->Authority.Data.Base,queue->Width,queue->Height) ||
        !kernel_vgpu_scanout_bind(queue,resource) || !checkpoint_private(queue,1)) return 0;
    for (uint32_t y=24; y<56; ++y) for (uint32_t x=16; x<80; ++x)
        pixels[(uint64_t)y*queue->Width+x]=0xffe040a0;
    if (!kernel_vgpu_scanout_update(queue,resource,16,24,64,32) || !checkpoint_private(queue,2) ||
        !kernel_vgpu_scanout_unbind(queue,resource) || !kernel_vgpu_resource_close(queue,resource)) return 0;
    return queue->Sequence==11 && kernel_log(0,"KERNEL_VGPU_SCANOUT_OK");
}
