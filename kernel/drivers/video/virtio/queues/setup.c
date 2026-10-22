#include "queue.h"
#include "lib/memory/address_ranges.h"

// ====== kernel_vgpu_queue_access ======

int kernel_vgpu_queue_access(VGPU_QUEUE *queue,uint32_t region,uint32_t offset,uint32_t width,uint32_t write,uint32_t *value)
{
    if (!queue || queue->Self!=(uint64_t)queue || !value ||
        !queue->Io.Access(queue->Io.Context,region,offset,width,write,value)) {
        if (queue && queue->Self==(uint64_t)queue) queue->Fault=VGPU_QUEUE_IO;
        return 0;
    }
    return 1;
}

// ====== set_private ======

static int set_private(VGPU_QUEUE *queue,uint32_t offset,uint32_t width,uint32_t expected)
{
    uint32_t value=expected;
    return kernel_vgpu_queue_access(queue,0,offset,width,1,&value) &&
        kernel_vgpu_queue_access(queue,0,offset,width,0,&value) && value==expected;
}

// ====== valid_private ======

static int valid_private(VGPU_QUEUE *queue,const VGPU_QUEUE_IO_CONTEXT *io,const VGPU_BUFFERS *buffers)
{
    if (!queue || !io || !buffers || !io->Context || !io->Access || !io->Clock || !io->Barrier ||
        (io->Mask!=0xffffff && io->Mask!=UINT32_MAX) || !io->Deadline || io->Deadline>=io->Mask/2 ||
        !io->Samples || io->Samples>1000000 || buffers->Reserved || buffers->Bits<32 || buffers->Bits>52 ||
        os_address_ranges_overlap((uintptr_t)queue,sizeof(*queue),(uintptr_t)io,sizeof(*io)) ||
        os_address_ranges_overlap((uintptr_t)queue,sizeof(*queue),(uintptr_t)buffers,sizeof(*buffers))) return 0;
    for (uint32_t byte=0; byte<sizeof(*queue); ++byte) if (((const unsigned char *)queue)[byte]) return 0;
    for (uint32_t page=0; page<4; ++page) {
        uint64_t address=(uintptr_t)buffers->Views[page], physical=buffers->Physical[page];
        if (!address || (address&4095) || address>UINT64_MAX-4096 || physical<4096 || (physical&4095) ||
            physical>(1ULL<<buffers->Bits)-4096 ||
            os_address_ranges_overlap(address,4096,(uintptr_t)queue,sizeof(*queue)) ||
            os_address_ranges_overlap(address,4096,(uintptr_t)io,sizeof(*io)) ||
            os_address_ranges_overlap(address,4096,(uintptr_t)buffers,sizeof(*buffers))) return 0;
        for (uint32_t other=0; other<page; ++other)
            if (physical==buffers->Physical[other] || address==(uintptr_t)buffers->Views[other]) return 0;
    }
    return 1;
}

// ====== configure_private ======

// VirtIO1.2 4.1.4.3: select0x16,size0x18,vector0x1a,enable0x1c,notify0x1e.
// Ring addresses0x20/0x28/0x30 are written as scalar low/high u32 before enable.
static int configure_private(VGPU_QUEUE *queue,uint32_t index)
{
    uint32_t value;
    if (!set_private(queue,0x16,2,index) || !kernel_vgpu_queue_access(queue,0,0x1c,2,0,&value) || value ||
        !kernel_vgpu_queue_access(queue,0,0x18,2,0,&value) || !kernel_vgpu_queue_size(value) ||
        !set_private(queue,0x18,2,8) || !set_private(queue,0x1a,2,0xffff) ||
        !kernel_vgpu_queue_access(queue,0,0x1e,2,0,&queue->Notify[index])) return 0;
    const uint32_t offsets[3]={0,128,160};
    for (uint32_t item=0; item<3; ++item) {
        uint64_t address=queue->Buffers.Physical[index]+offsets[item];
        if (!set_private(queue,0x20+item*8,4,(uint32_t)address) ||
            !set_private(queue,0x24+item*8,4,(uint32_t)(address>>32))) return 0;
    }
    return set_private(queue,0x1c,2,1);
}

// ====== kernel_vgpu_queue_start ======

// VirtIO1.2 3.1.1: DRIVER_OK bit2 only after both queues are configured.
int kernel_vgpu_queue_start(VGPU_QUEUE *queue,const VGPU_QUEUE_IO_CONTEXT *io,const VGPU_BUFFERS *buffers)
{
    uint32_t value;
    if (!valid_private(queue,io,buffers)) return 0;
    queue->Self=(uintptr_t)queue; queue->Io=*io; queue->Buffers=*buffers;
    queue->State=2;
    if (!kernel_vgpu_queue_access(queue,0,20,1,0,&value) || value!=11 ||
        !kernel_vgpu_queue_access(queue,0,0x12,2,0,&value) || value<2) goto failure;
    for (uint32_t page=0; page<4; ++page)
        for (uint32_t byte=0; byte<4096; ++byte) buffers->Views[page][byte]=0;
    for (uint32_t index=0; index<2; ++index) {
        volatile VGPU_RING *ring=(volatile VGPU_RING *)buffers->Views[index];
        ring->Flags=1;
        if (!configure_private(queue,index)) goto failure;
    }
    if (!set_private(queue,0x10,2,0xffff) || !io->Barrier(io->Context) || !set_private(queue,20,1,15)) goto failure;
    queue->State=1;
    return 1;
failure:
    if (!queue->Fault) queue->Fault=VGPU_QUEUE_STATE;
    return 0;
}
