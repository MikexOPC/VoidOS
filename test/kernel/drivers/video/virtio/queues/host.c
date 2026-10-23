#include "kernel/drivers/video/virtio/queues/queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned long checks;
#define CHECK(x) do { ++checks; if (!(x)) { printf("FAIL line %u\n",(unsigned)__LINE__); exit(1); } } while (0)
__declspec(align(4096)) static unsigned char pages[4][4096];
typedef struct {
    uint32_t  Common[56], Tick, Calls, FailAt, Corrupt, Frozen, Stall, Barriers, FailBarrier;
    uint32_t  ClockFail, Status, Select, Size[2], Enabled[2], Invalid;
} MOCK;
static MOCK mock;
static VGPU_QUEUE queue;

// ====== word_private ======

static void word_private(unsigned char *data,uint32_t offset,uint32_t value)
{
    for (uint32_t byte=0; byte<4; ++byte) data[offset+byte]=(unsigned char)(value>>(byte*8));
}

// ====== access_private ======

static int access_private(void *opaque,uint32_t region,uint32_t offset,uint32_t width,uint32_t write,uint32_t *value)
{
    MOCK *state=opaque;
    ++state->Calls;
    if (state->FailAt && state->Calls==state->FailAt) return 0;
    if (region==1) {
        CHECK(write==1 && width==2 && *value==0 && offset==3);
        volatile VGPU_RING *ring=(volatile VGPU_RING *)pages[0];
        uint32_t command=*(uint32_t *)pages[2];
        uint32_t expected=command==0x100?408:24;
        CHECK(ring->Descriptors[0].Address==0x1005000 && kernel_vgpu_queue_message(command,ring->Descriptors[0].Length-24) &&
            ring->Descriptors[0].Flags==1 && ring->Descriptors[0].Next==1);
        CHECK(ring->Descriptors[1].Address==0x1007000 && ring->Descriptors[1].Length==expected && ring->Descriptors[1].Flags==2);
        CHECK(ring->Heads[(ring->Available-1U)%8]==0 && ring->Available==(uint16_t)(ring->Used+1U));
        if (state->Stall) return 1;
        word_private(pages[3],0,command==0x100?0x1101:0x1100); word_private(pages[3],4,1);
        memcpy(pages[3]+8,pages[2]+8,8);
        if (command==0x100) { word_private(pages[3],32,1024); word_private(pages[3],36,768); word_private(pages[3],40,1); }
        if (state->Corrupt==4) pages[3][1]=0;
        if (state->Corrupt==5) pages[3][8]^=1;
        if (state->Corrupt==6) word_private(pages[3],32,0);
        ring->Entries[ring->Used%8]=(VGPU_USED){state->Corrupt==1?7:0,state->Corrupt==2?409:expected};
        ring->Used=(uint16_t)(ring->Used+(state->Corrupt==3?2:1));
        return 1;
    }
    CHECK(region==0 && kernel_vgpu_queue_register(offset,width,write));
    CHECK(offset<56);
    if (offset==20) { if (write) state->Status=*value; else *value=state->Status; return 1; }
    if (offset==0x12) { *value=2; return 1; }
    if (offset==0x16) { if (write) state->Select=*value; else *value=state->Select; return 1; }
    if (offset==0x18) { if (write) state->Size[state->Select]=*value; else *value=state->Size[state->Select]; return 1; }
    if (offset==0x1c) { if (write) state->Enabled[state->Select]=*value; else *value=state->Enabled[state->Select]; return 1; }
    if (offset==0x1e) { *value=3+state->Select; return 1; }
    if (write) state->Common[offset]=*value; else *value=state->Common[offset];
    return 1;
}

// ====== clock_private ======

static int clock_private(void *opaque,uint32_t *tick)
{
    MOCK *state=opaque;
    if (state->ClockFail) return 0;
    if (!state->Frozen) state->Tick=(state->Tick+1)&0xffffff;
    *tick=state->Tick;
    return 1;
}

// ====== barrier_private ======

static int barrier_private(void *opaque)
{
    MOCK *state=opaque;
    return ++state->Barriers!=state->FailBarrier;
}

// ====== prepare_private ======

static int prepare_private(void)
{
    VGPU_QUEUE_IO_CONTEXT io={&mock,access_private,clock_private,barrier_private,0xffffff,100,200};
    VGPU_BUFFERS buffers={0};
    memset(&queue,0,sizeof(queue)); memset(&mock,0,sizeof(mock)); memset(pages,0,sizeof(pages));
    mock.Status=11; mock.Size[0]=mock.Size[1]=256;
    buffers.Bits=32;
    // Device addresses differ from the frames, as with an IOMMU: only Device may reach the device.
    for (uint32_t index=0; index<4; ++index) {
        buffers.Views[index]=pages[index]; buffers.Physical[index]=0x100000+index*4096; buffers.Device[index]=0x1001000+index*8192;
    }
    return kernel_vgpu_queue_start(&queue,&io,&buffers);
}

// ====== main ======

int main(void)
{
    CHECK(prepare_private());
    CHECK(queue.State==1 && mock.Status==15 && mock.Size[0]==8 && mock.Size[1]==8);
    for (uint32_t index=0; index<70000; ++index) CHECK(kernel_vgpu_queue_exchange(&queue));
    CHECK(queue.Sequence==70000 && queue.Width==1024 && queue.Height==768);
    uint32_t exchange_calls=mock.Calls;
    CHECK(!kernel_vgpu_queue_exchange(0));
    VGPU_QUEUE copy=queue;
    CHECK(!kernel_vgpu_queue_exchange(&copy));
    for (uint32_t failure=1; failure<=6; ++failure) {
        CHECK(prepare_private()); mock.Corrupt=failure;
        CHECK(!kernel_vgpu_queue_exchange(&queue) && queue.Fault==VGPU_QUEUE_RESPONSE);
        CHECK(!kernel_vgpu_queue_exchange(&queue));
    }
    CHECK(prepare_private()); mock.Stall=1;
    CHECK(!kernel_vgpu_queue_exchange(&queue) && queue.Fault==VGPU_QUEUE_TIME);
    CHECK(prepare_private()); mock.Frozen=1;
    CHECK(!kernel_vgpu_queue_exchange(&queue) && queue.Fault==VGPU_QUEUE_TIME);
    CHECK(prepare_private()); mock.ClockFail=1;
    CHECK(!kernel_vgpu_queue_exchange(&queue));
    CHECK(prepare_private()); mock.Tick=0xfffffd;
    CHECK(kernel_vgpu_queue_exchange(&queue));
    for (uint32_t failure=1; failure<=4; ++failure) {
        CHECK(prepare_private()); mock.FailBarrier=mock.Barriers+failure;
        CHECK(!kernel_vgpu_queue_exchange(&queue));
    }
    CHECK(prepare_private()); uint32_t setup_calls=mock.Calls;
    for (uint32_t failure=1; failure<=setup_calls; ++failure) {
        CHECK(prepare_private());
        VGPU_QUEUE_IO_CONTEXT io=queue.Io; VGPU_BUFFERS buffers=queue.Buffers;
        memset(&queue,0,sizeof(queue)); memset(&mock,0,sizeof(mock));
        mock.Status=11; mock.Size[0]=mock.Size[1]=256; mock.FailAt=failure;
        CHECK(!kernel_vgpu_queue_start(&queue,&io,&buffers));
    }
    CHECK(prepare_private()); uint32_t before=mock.Calls;
    CHECK(kernel_vgpu_queue_exchange(&queue)); uint32_t per_exchange=mock.Calls-before;
    for (uint32_t failure=1; failure<=per_exchange; ++failure) {
        CHECK(prepare_private()); mock.FailAt=mock.Calls+failure;
        CHECK(!kernel_vgpu_queue_exchange(&queue));
    }
    CHECK(prepare_private()); VGPU_BUFFERS buffers=queue.Buffers; VGPU_QUEUE_IO_CONTEXT io=queue.Io;
    memset(&queue,0,sizeof(queue)); buffers.Physical[3]=buffers.Physical[0];
    CHECK(!kernel_vgpu_queue_start(&queue,&io,&buffers));
    for (uint32_t bad=0; bad<4; ++bad) {
        CHECK(prepare_private()); buffers=queue.Buffers; memset(&queue,0,sizeof(queue));
        const uint64_t values[4]={0,0x1001001,buffers.Device[0],1ULL<<32};
        buffers.Device[3]=values[bad];
        CHECK(!kernel_vgpu_queue_start(&queue,&io,&buffers));
    }
    CHECK(prepare_private()); unsigned char response[408]; uint32_t width=77,height=88;
    CHECK(kernel_vgpu_queue_exchange(&queue)); memcpy(response,pages[3],408);
    CHECK(!kernel_vgpu_queue_response(response,407,1,&width,&height) && width==77 && height==88);
    CHECK(!kernel_vgpu_queue_response(response,408,2,&width,&height) && width==77 && height==88);
    CHECK(!kernel_vgpu_queue_response(response,408,1,&width,&width));
    uint32_t selected=99;
    memcpy(response+24+7*24,response+24,24); memset(response+24,0,24);
    CHECK(kernel_vgpu_queue_response_selected(response,408,1,&width,&height,&selected) && selected==7 && width==1024 && height==768);
    CHECK(!kernel_vgpu_queue_response_selected(response,408,1,&width,&height,&width));
    CHECK(!kernel_vgpu_queue_response_selected(response,408,1,&width,&height,(uint32_t *)response));
    const uint32_t commands[7]={0x101,0x106,0x105,0x107,0x102,0x103,0x104}, lengths[7]={16,24,32,8,8,24,24};
    unsigned char body[32]={0};
    for (uint32_t command=0; command<7; ++command) {
        CHECK(prepare_private());
        CHECK(kernel_vgpu_queue_control(&queue,commands[command],body,lengths[command]) && queue.Sequence==1);
        CHECK(!kernel_vgpu_queue_control(&queue,commands[command],body,lengths[command]+1));
        for (uint32_t failure=1; failure<=5; ++failure) {
            CHECK(prepare_private()); mock.Corrupt=failure;
            CHECK(!kernel_vgpu_queue_control(&queue,commands[command],body,lengths[command]));
        }
    }
    CHECK(prepare_private());
    CHECK(!kernel_vgpu_queue_control(&queue,0x101,pages[2],16));
    CHECK(!kernel_vgpu_queue_control(&queue,0x101,0,16));
    printf("PASS %lu queue host checks, 70000 completions, %u initial run IO accesses\n",checks,exchange_calls);
    return 0;
}
