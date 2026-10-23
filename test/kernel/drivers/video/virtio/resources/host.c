#include "kernel/drivers/video/virtio/resources/resource.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks, calls, fail_at;
static const uint32_t commands[5]={0x101,0x106,0x105,0x107,0x102};
#define CHECK(x) do { ++checks; if (!(x)) {printf("FAIL resource:%u\n",(unsigned)__LINE__);exit(1);} } while (0)

// ====== word_private ======

static uint32_t word_private(const unsigned char *data,uint32_t at)
{
    return (uint32_t)data[at]|(uint32_t)data[at+1]<<8|(uint32_t)data[at+2]<<16|(uint32_t)data[at+3]<<24;
}

// ====== kernel_vgpu_queue_control ======

int kernel_vgpu_queue_control(VGPU_QUEUE *queue,uint32_t command,const unsigned char *body,uint32_t bytes)
{
    CHECK(command==commands[calls] && queue->State==1); ++calls;
    if (command==0x101) CHECK(bytes==16 && word_private(body,0)==1 && word_private(body,4)==1 &&
        word_private(body,8)==1280 && word_private(body,12)==800);
    if (command==0x106) CHECK(bytes==24 && word_private(body,0)==1 && word_private(body,4)==1 &&
        word_private(body,8)==0x1100000 && !word_private(body,12) && word_private(body,16)==4096000 && !word_private(body,20));
    if (command==0x105) CHECK(bytes==32 && !word_private(body,0) && !word_private(body,4) &&
        word_private(body,8)==1280 && word_private(body,12)==800 && !word_private(body,16) &&
        !word_private(body,20) && word_private(body,24)==1 && !word_private(body,28));
    if (command==0x107 || command==0x102) CHECK(bytes==8 && word_private(body,0)==1 && !word_private(body,4));
    return calls!=fail_at;
}

// ====== main ======

int main(void)
{
    for (unsigned cycle=0; cycle<100; ++cycle) for (fail_at=0; fail_at<=5; ++fail_at) {
        VGPU_QUEUE queue={0}; VGPU_RESOURCE resource={0};
        queue.Self=(uint64_t)&queue; queue.State=1; queue.Buffers.Bits=52; calls=0;
        for (unsigned page=0; page<4; ++page) { queue.Buffers.Physical[page]=0x100000+page*4096; queue.Buffers.Device[page]=0x1001000+page*8192; }
        CHECK(kernel_vgpu_resource_run(&queue,&resource,0x200000,0x1100000,1280,800)==(fail_at==0));
        CHECK(resource.State==(fail_at?UINT32_MAX:5));
        CHECK(calls==(fail_at?fail_at:5));
        CHECK(!kernel_vgpu_resource_run(&queue,&resource,0x200000,0x1100000,1280,800));
        memset(&resource,0,sizeof(resource));
        CHECK(!kernel_vgpu_resource_run(&queue,&resource,0x100000,0x1100000,1280,800) && !resource.Self);
        CHECK(!kernel_vgpu_resource_run(&queue,&resource,0x200001,0x1100000,1280,800));
        CHECK(!kernel_vgpu_resource_run(&queue,&resource,0x200000,0x1100000,4096,4096));
        CHECK(!kernel_vgpu_resource_run(&queue,&resource,0x200000,0x1100001,1280,800));
        CHECK(!kernel_vgpu_resource_run(&queue,&resource,0x200000,0x1000000,1280,800) && !resource.Self);
        CHECK(!kernel_vgpu_resource_run(&queue,&resource,0x200000,0,1280,800));
    }
    printf("PASS %u resource protocol checks, 600 lifecycles, every command failure and immutable rejection\n",checks);
    return 0;
}
