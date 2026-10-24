#include "scanout.h"

// ====== word_private ======

static void word_private(unsigned char *body,uint32_t at,uint32_t value)
{
    for (uint32_t index=0; index<4; ++index) body[at+index]=(unsigned char)(value>>(index*8));
}

// ====== valid_private ======

static int valid_private(const VGPU_QUEUE *queue,const VGPU_RESOURCE *resource,uint32_t bound)
{
    return queue && queue->Self==(uintptr_t)queue && queue->State==1 && !queue->Fault && queue->Scanout<16 &&
        resource && resource->Self==(uintptr_t)resource && resource->Queue==(uintptr_t)queue && resource->State==3 &&
        queue->Width==resource->Width && queue->Height==resource->Height &&
        resource->Id==1 && resource->Bound==bound && resource->Bytes==kernel_vgpu_surface_bytes(resource->Width,resource->Height) &&
        resource->Bytes && (!bound || resource->Scanout==queue->Scanout);
}

// ====== rectangle_private ======

static void rectangle_private(unsigned char *body,uint32_t x,uint32_t y,uint32_t width,uint32_t height)
{
    word_private(body,0,x); word_private(body,4,y); word_private(body,8,width); word_private(body,12,height);
}

// ====== kernel_vgpu_scanout_bind ======

// VirtIO1.2 5.7.6.8: SET_SCANOUT0x103, rectangle at24, scanout40/resource44.
// FLUSH0x104, rectangle24/resource40/padding44; FENCE bit0 in queue transport.
int kernel_vgpu_scanout_bind(VGPU_QUEUE *queue,VGPU_RESOURCE *resource)
{
    unsigned char body[24]={0};
    if (!valid_private(queue,resource,0)) return 0;
    resource->Scanout=queue->Scanout; resource->Bound=UINT32_MAX;
    rectangle_private(body,0,0,resource->Width,resource->Height);
    word_private(body,16,resource->Scanout); word_private(body,20,resource->Id);
    if (!kernel_vgpu_queue_control(queue,0x103,body,24)) return 0;
    word_private(body,16,resource->Id); word_private(body,20,0);
    if (!kernel_vgpu_queue_control(queue,0x104,body,24)) return 0;
    resource->Bound=1;
    return 1;
}

// ====== kernel_vgpu_scanout_update ======

// VirtIO1.2 5.7.6.2/8: transfer offset40/resource48; row stride is full resource width.
int kernel_vgpu_scanout_update(VGPU_QUEUE *queue,VGPU_RESOURCE *resource,uint32_t x,uint32_t y,uint32_t width,uint32_t height)
{
    unsigned char body[32]={0};
    if (!valid_private(queue,resource,1)) return 0;
    uint64_t offset=kernel_vgpu_surface_offset(resource->Width,resource->Height,x,y,width,height);
    if (offset==UINT64_MAX) return 0;
    rectangle_private(body,x,y,width,height);
    word_private(body,16,(uint32_t)offset); word_private(body,20,(uint32_t)(offset>>32));
    word_private(body,24,resource->Id); resource->Bound=UINT32_MAX;
    if (!kernel_vgpu_queue_control(queue,0x105,body,32)) return 0;
    word_private(body,16,resource->Id); word_private(body,20,0); word_private(body,24,0);
    if (!kernel_vgpu_queue_control(queue,0x104,body,24)) return 0;
    resource->Bound=1;
    return 1;
}

// ====== kernel_vgpu_scanout_post ======

// R1-A: the update of kernel_vgpu_scanout_update split in two posted commands. Phase 0 posts
// TRANSFER_TO_HOST_2D (Bound becomes uncertain), phase 1 posts RESOURCE_FLUSH of the same
// rectangle after the transfer completed; kernel_vgpu_scanout_settle confirms the flush.
int kernel_vgpu_scanout_post(VGPU_QUEUE *queue,VGPU_RESOURCE *resource,uint32_t phase,
    uint32_t x,uint32_t y,uint32_t width,uint32_t height)
{
    unsigned char body[32]={0};
    if (phase>1 || !valid_private(queue,resource,phase ? UINT32_MAX : 1)) return 0;
    uint64_t offset=kernel_vgpu_surface_offset(resource->Width,resource->Height,x,y,width,height);
    if (offset==UINT64_MAX) return 0;
    rectangle_private(body,x,y,width,height);
    if (!phase) {
        word_private(body,16,(uint32_t)offset); word_private(body,20,(uint32_t)(offset>>32));
        word_private(body,24,resource->Id); resource->Bound=UINT32_MAX;
        return kernel_vgpu_queue_submit(queue,0x105,body,32);
    }
    word_private(body,16,resource->Id);
    return kernel_vgpu_queue_submit(queue,0x104,body,24);
}

// ====== kernel_vgpu_scanout_settle ======

int kernel_vgpu_scanout_settle(const VGPU_QUEUE *queue,VGPU_RESOURCE *resource)
{
    if (!valid_private(queue,resource,UINT32_MAX)) return 0;
    resource->Bound=1;
    return 1;
}

// ====== kernel_vgpu_scanout_unbind ======

// VirtIO1.2 5.7.6.8: resource_id0 disables scanout; retain backing until success or reset.
int kernel_vgpu_scanout_unbind(VGPU_QUEUE *queue,VGPU_RESOURCE *resource)
{
    unsigned char body[24]={0};
    if (!valid_private(queue,resource,1)) return 0;
    word_private(body,16,resource->Scanout); resource->Bound=UINT32_MAX;
    if (!kernel_vgpu_queue_control(queue,0x103,body,24)) return 0;
    resource->Bound=0;
    return 1;
}
