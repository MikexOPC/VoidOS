#include "resource.h"
#include "lib/memory/address_ranges.h"

// ====== word_private ======

static void word_private(unsigned char *bytes,uint32_t at,uint32_t value)
{
    for (uint32_t index=0; index<4; ++index) bytes[at+index]=(unsigned char)(value>>(index*8));
}

// ====== command_private ======

static int command_private(VGPU_QUEUE *queue,VGPU_RESOURCE *resource,uint32_t command)
{
    unsigned char body[32]={0};
    uint32_t bytes=8, next=kernel_vgpu_resource_transition(resource->State,command);
    if (next==UINT32_MAX) return 0;
    word_private(body,0,resource->Id);
    if (command==0x101) {
        bytes=16; word_private(body,4,1); word_private(body,8,resource->Width); word_private(body,12,resource->Height);
    } else if (command==0x106) {
        bytes=24; word_private(body,4,1); word_private(body,8,(uint32_t)resource->Base);
        word_private(body,12,(uint32_t)(resource->Base>>32)); word_private(body,16,(uint32_t)resource->Bytes);
    } else if (command==0x105) {
        bytes=32; word_private(body,0,0); word_private(body,8,resource->Width); word_private(body,12,resource->Height);
        word_private(body,24,resource->Id);
#ifdef KERNEL_TEST_VGPU_RESOURCE_REJECT
        word_private(body,24,resource->Id+1);
#endif
    }
    resource->State=UINT32_MAX;
    if (!kernel_vgpu_queue_control(queue,command,body,bytes)) return 0;
    resource->State=next;
    return 1;
}

// ====== kernel_vgpu_resource_open ======

// VirtIO1.2 5.7.6.8: CREATE101,ATTACH106,TRANSFER105,DETACH107,UNREF102.
// A single contiguous owned backing entry; FENCE bit0 enforced by queue transport.
int kernel_vgpu_resource_open(VGPU_QUEUE *queue,VGPU_RESOURCE *resource,uint64_t physical,uint32_t width,uint32_t height)
{
    uint64_t bytes=kernel_vgpu_surface_bytes(width,height);
    if (!queue || queue->Self!=(uintptr_t)queue || queue->State!=1 || queue->Fault ||
        !resource || !bytes || physical<4096 || (physical&4095) ||
        queue->Buffers.Bits<32 || queue->Buffers.Bits>52 || physical>(1ULL<<queue->Buffers.Bits)-bytes ||
        os_address_ranges_overlap((uintptr_t)queue,sizeof(*queue),(uintptr_t)resource,sizeof(*resource))) return 0;
    for (uint32_t byte=0; byte<sizeof(*resource); ++byte) if (((const unsigned char *)resource)[byte]) return 0;
    for (uint32_t page=0; page<4; ++page)
        if (os_address_ranges_overlap(physical,bytes,queue->Buffers.Physical[page],4096) ||
            os_address_ranges_overlap((uintptr_t)resource,sizeof(*resource),(uintptr_t)queue->Buffers.Views[page],4096)) return 0;
    *resource=(VGPU_RESOURCE){(uintptr_t)resource,physical,bytes,(uintptr_t)queue,width,height,1,0,0,0};
    const uint32_t commands[3]={0x101,0x106,0x105};
    for (uint32_t index=0; index<3; ++index) if (!command_private(queue,resource,commands[index])) return 0;
    return resource->State==3;
}

// ====== kernel_vgpu_resource_close ======

int kernel_vgpu_resource_close(VGPU_QUEUE *queue,VGPU_RESOURCE *resource)
{
    if (!queue || queue->Self!=(uintptr_t)queue || queue->State!=1 || queue->Fault || !resource ||
        resource->Self!=(uintptr_t)resource || resource->Queue!=(uintptr_t)queue || resource->State!=3 || resource->Bound) return 0;
    return command_private(queue,resource,0x107) && command_private(queue,resource,0x102);
}

// ====== kernel_vgpu_resource_run ======

int kernel_vgpu_resource_run(VGPU_QUEUE *queue,VGPU_RESOURCE *resource,uint64_t physical,uint32_t width,uint32_t height)
{
    return kernel_vgpu_resource_open(queue,resource,physical,width,height) && kernel_vgpu_resource_close(queue,resource);
}
