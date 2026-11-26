#include "queue.h"
#include "lib/memory/address_ranges.h"

// ====== barrier_private ======

static int barrier_private(VGPU_QUEUE *queue)
{
    if (queue->Io.Barrier(queue->Io.Context)) return 1;
    queue->Fault=VGPU_QUEUE_IO;
    return 0;
}

// ====== request_private ======

// VirtIO1.2 5.7.6.7/8: GET_DISPLAY_INFO0x100, FENCE bit0, header24 bytes.
static void request_private(VGPU_QUEUE *queue,uint32_t command,const unsigned char *body,uint32_t bytes)
{
    volatile uint32_t *request=(volatile uint32_t *)queue->Buffers.Views[2];
    uint64_t fence=queue->Sequence+1;
    request[0]=command; request[1]=1;
    request[2]=(uint32_t)fence; request[3]=(uint32_t)(fence>>32);
    request[4]=0; request[5]=0;
    for (uint32_t byte=0; byte<bytes; ++byte) queue->Buffers.Views[2][24+byte]=body[byte];
    for (uint32_t byte=0; byte<408; ++byte) queue->Buffers.Views[3][byte]=0;
}

// ====== complete_private ======

static int complete_private(VGPU_QUEUE *queue,uint16_t current)
{
    volatile VGPU_RING *ring=(volatile VGPU_RING *)queue->Buffers.Views[0];
    unsigned char response[408];
    uint32_t id=ring->Entries[queue->Used%8].Id, bytes=ring->Entries[queue->Used%8].Length;
    if (!kernel_vgpu_queue_completion_sized(queue->Used,current,id,bytes,queue->Expected)) return 0;
    for (uint32_t byte=0; byte<queue->Expected; ++byte) response[byte]=queue->Buffers.Views[3][byte];
    if (!barrier_private(queue) || ring->Used!=current) return 0;
    if (queue->Expected==408) {
        if (!kernel_vgpu_queue_response_selected(response,408,queue->Sequence+1,&queue->Width,&queue->Height,&queue->Scanout)) return 0;
    } else {
        uint64_t fence=0;
        for (uint32_t byte=0; byte<8; ++byte) fence|=(uint64_t)response[8+byte]<<(byte*8);
        if (response[0] || response[1]!=0x11 || response[2] || response[3] || response[4]!=1 ||
            response[5] || response[6] || response[7] || fence!=queue->Sequence+1) return 0;
        for (uint32_t byte=16; byte<24; ++byte) if (response[byte]) return 0;
    }
    queue->Used=current; ++queue->Sequence; queue->State=1;
    return 1;
}

// ====== kernel_vgpu_queue_poll ======

// VirtIO1.2 2.7.14: observe used.idx before reading the completion and payload. One sample:
// 1 completed (State 1), 0 still in flight, -1 fault (Fault set). limit: ticks since the submit.
int kernel_vgpu_queue_poll(VGPU_QUEUE *queue,uint32_t limit)
{
    uint32_t now, status;
    if (!queue || queue->Self!=(uintptr_t)queue || queue->State!=2 || queue->Fault ||
        !limit || limit>=queue->Io.Mask/2) return -1;
    volatile VGPU_RING *ring=(volatile VGPU_RING *)queue->Buffers.Views[0];
    uint32_t start=queue->Started;
    if (!queue->Io.Clock(queue->Io.Context,&now) || now>queue->Io.Mask) { queue->Fault=VGPU_QUEUE_TIME; return -1; }
    uint16_t current=ring->Used;
    if (!barrier_private(queue)) return -1;
    if (!kernel_vgpu_queue_access(queue,0,20,1,0,&status)) return -1;
    if (status!=15) { queue->Fault=VGPU_QUEUE_STATE; return -1; }
    if (current!=queue->Used && now!=start) {
        queue->LastWait=(now-start)&queue->Io.Mask;
        if (complete_private(queue,current)) return 1;
        queue->Fault=VGPU_QUEUE_RESPONSE; return -1;
    }
    if (((now-start)&queue->Io.Mask)>=limit) { queue->Fault=VGPU_QUEUE_TIME; return -1; }
    return 0;
}

// ====== wait_private ======

// Blocking form for bring-up and teardown: Io.Samples polls within Io.Deadline.
static int wait_private(VGPU_QUEUE *queue)
{
    for (uint32_t sample=0; sample<queue->Io.Samples; ++sample) {
        int result=kernel_vgpu_queue_poll(queue,queue->Io.Deadline);
        if (result) return result>0;
    }
    queue->Fault=VGPU_QUEUE_TIME;
    return 0;
}

// ====== kernel_vgpu_queue_submit ======

// VirtIO1.2 2.7.5: NEXT bit0, WRITE bit1; request readable, response writable. Posts one command
// and notifies; the completion is observed by kernel_vgpu_queue_poll (State 2 until then).
int kernel_vgpu_queue_submit(VGPU_QUEUE *queue,uint32_t command,const unsigned char *body,uint32_t bytes)
{
    uint32_t start, value;
    if (!queue || queue->Self!=(uintptr_t)queue || queue->State!=1 || queue->Fault || queue->Sequence==UINT64_MAX) return 0;
    if (!kernel_vgpu_queue_message(command,bytes) || (bytes && !body) ||
        (bytes && os_address_ranges_overlap((uintptr_t)body,bytes,(uintptr_t)queue,sizeof(*queue)))) return 0;
    for (uint32_t page=0; bytes && page<4; ++page)
        if (os_address_ranges_overlap((uintptr_t)body,bytes,(uintptr_t)queue->Buffers.Views[page],4096)) return 0;
    volatile VGPU_RING *ring=(volatile VGPU_RING *)queue->Buffers.Views[0];
    if (!kernel_vgpu_queue_access(queue,0,20,1,0,&value) || value!=15 ||
        !queue->Io.Clock(queue->Io.Context,&start) || start>queue->Io.Mask ||
        ring->Available!=queue->Available || ring->Used!=queue->Used) {
        queue->Fault=VGPU_QUEUE_STATE; return 0;
    }
    queue->State=2;
    queue->Expected=command==0x100?408:24;
    request_private(queue,command,body,bytes);
    ring->Descriptors[0]=(VGPU_DESCRIPTOR){queue->Buffers.Device[2],24+bytes,1,1};
    ring->Descriptors[1]=(VGPU_DESCRIPTOR){queue->Buffers.Device[3],queue->Expected,2,0};
    ring->Heads[queue->Available%8]=0;
    if (!barrier_private(queue)) return 0;
    queue->Available=(uint16_t)(queue->Available+1U); ring->Available=queue->Available;
    if (!barrier_private(queue)) return 0;
    value=0;
    queue->Started=start;
    return kernel_vgpu_queue_access(queue,1,queue->Notify[0],2,1,&value);
}

// ====== exchange_private ======

static int exchange_private(VGPU_QUEUE *queue,uint32_t command,const unsigned char *body,uint32_t bytes)
{
    return kernel_vgpu_queue_submit(queue,command,body,bytes) && wait_private(queue);
}

// ====== kernel_vgpu_queue_exchange ======

int kernel_vgpu_queue_exchange(VGPU_QUEUE *queue) { return exchange_private(queue,0x100,0,0); }

// ====== kernel_vgpu_queue_control ======

int kernel_vgpu_queue_control(VGPU_QUEUE *queue,uint32_t command,const unsigned char *body,uint32_t bytes)
{
    if (command==0x100) return 0;
    return exchange_private(queue,command,body,bytes);
}

// ====== kernel_vgpu_cursor_post ======

// VirtIO 1.2 Section 5.7.6.10: VIRTIO_GPU_CMD_UPDATE_CURSOR (0x0300) / MOVE_CURSOR (0x0301) on cursorq (queue 1).
// Request layout (56 bytes): ctrl_hdr (24B) + cursor_pos {scanout_id, x, y, pad} (16B) + resource_id, hot_x, hot_y, pad (16B).
int kernel_vgpu_cursor_post(VGPU_QUEUE *queue,uint32_t command,uint32_t x,uint32_t y,uint32_t resource_id,uint32_t hot_x,uint32_t hot_y)
{
    if (!queue || queue->Self!=(uintptr_t)queue || (queue->State!=1 && queue->State!=2) || queue->Fault ||
        (command!=0x0300U && command!=0x0301U)) return 0;
    volatile uint32_t *req=(volatile uint32_t *)(queue->Buffers.Views[1] + 2048U);
    req[0]=command;
    req[1]=0;
    req[2]=0;
    req[3]=0;
    req[4]=0;
    req[5]=0;
    req[6]=queue->Scanout;
    req[7]=x;
    req[8]=y;
    req[9]=0;
    req[10]=resource_id;
    req[11]=hot_x;
    req[12]=hot_y;
    req[13]=0;
    volatile VGPU_RING *ring=(volatile VGPU_RING *)queue->Buffers.Views[1];
    ring->Descriptors[0]=(VGPU_DESCRIPTOR){queue->Buffers.Device[1] + 2048ULL, 56U, 0U, 0U};
    ring->Heads[queue->CursorAvailable % 8U]=0U;
    if (!barrier_private(queue)) return 0;
    queue->CursorAvailable=(uint16_t)(queue->CursorAvailable + 1U);
    ring->Available=queue->CursorAvailable;
    if (!barrier_private(queue)) return 0;
    uint32_t value=1U;
    return kernel_vgpu_queue_access(queue,1,queue->Notify[1],2,1,&value);
}
